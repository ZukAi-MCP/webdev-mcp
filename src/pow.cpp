// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2021-2021 The Smileycoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>

/** [smly] Gets the next required nbits.
 * Uses the original Litecoin GetNextWorkRequired functionality prior to
 * the multi algo fork (218000). After the fork, calls the new
 * GetNextMultiAlgoWorkRequired function that handles the different algorithms.
 *
 * NOTE: Currently the wallet behaves like the 2.x.x wallets where the `bad-diffbits`
 * check is disabled in `src/validation.cpp` due to the GetNextMultiAlgoWorkRequired
 * function behaving exactly like it and does not match the correct pblock->nBits
 * for a large section of blocks from block 222524 and onward.
 */
unsigned int GetNextWorkRequired(const CBlockIndex *pindexLast, const CBlockHeader *pblock, const Consensus::Params &params)
{
    assert(pindexLast != nullptr);

    // [smly] Gate off original GetNextWorkRequired functionality
    if ((pindexLast->nHeight+1) < params.MultiAlgoForkHeight) {
        unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
        // Only change once per difficulty adjustment interval
        int64_t nDifficultyAdjustmentInterval;
        if (pindexLast->nHeight+1 < params.FirstTimespanChangeHeight) {
            nDifficultyAdjustmentInterval = params.nPowOriginalTargetTimespan / params.nPowTargetSpacing;
        } else {
            nDifficultyAdjustmentInterval = params.nPowTargetTimespan / params.nPowTargetSpacing;
        }
        if ((pindexLast->nHeight+1) % nDifficultyAdjustmentInterval != 0)
        {
            if (params.fPowAllowMinDifficultyBlocks)
            {
                // Special difficulty rule for testnet:
                // If the new block's timestamp is more than 2* 10 minutes
                // then allow mining of a min-difficulty block.
                if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                    return nProofOfWorkLimit;
                else
                {
                    // Return the last non-special-min-difficulty-rules-block
                    const CBlockIndex* pindex = pindexLast;
                    while (pindex->pprev && pindex->nHeight % nDifficultyAdjustmentInterval != 0 && pindex->nBits == nProofOfWorkLimit)
                        pindex = pindex->pprev;
                    return pindex->nBits;
                }
            }
            return pindexLast->nBits;
        }

        // Go back by what we want to be 14 days worth of blocks
        // Litecoin: This fixes an issue where a 51% attack can change difficulty at will.
        // Go back the full period unless it's the first retarget after genesis. Code courtesy of Art Forz
        int blockstogoback = nDifficultyAdjustmentInterval -1;
        if ((pindexLast->nHeight+1) != nDifficultyAdjustmentInterval)
            blockstogoback = nDifficultyAdjustmentInterval;

        // Go back by what we want to be 14 days worth of blocks
        const CBlockIndex* pindexFirst = pindexLast;
        for (int i = 0; pindexFirst && i < blockstogoback; i++)
            pindexFirst = pindexFirst->pprev;

        assert(pindexFirst);

        return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
    }

    return GetNextMultiAlgoWorkRequired(pindexLast, pblock, params);
}

unsigned int GetNextMultiAlgoWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    // [smly] No need to assert that pindexLast isn't null as that's handled by GetNextWorkRequired.
    int algo = pblock->GetAlgo();
    bool fDiffChange = pindexLast->nHeight >= params.DifficultyChangeForkHeight;
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
    //if (TestNet())
    //{
    //	// Testnet minimum difficulty block if it goes over normal block time.
    //	if (pblock->nTime > pindexLast->nTime + nTargetSpacing*2)
    //		return nProofOfWorkLimit;
    //	else
    //	{
    //		// Return the last non-special-min-difficulty-rules-block
    //		const CBlockIndex* pindex = pindexLast;
    //		while (pindex->pprev && pindex->nHeight % nInterval != 0 && pindex->nBits == nProofOfWorkLimit)
    //			pindex = pindex->pprev;
    //		return pindex->nBits;
    //	}
    //}

    // find first block in averaging interval
    // Go back by what we want to be nAveragingInterval blocks per algo
    const CBlockIndex* pindexFirst = pindexLast;
    for (int i = 0; pindexFirst && i < NUM_ALGOS * (fDiffChange ? params.nMultiAlgoAveragingIntervalV2 : params.nMultiAlgoAveragingInterval); i++) {
        pindexFirst = pindexFirst->pprev;
    }

    int64_t nMultiAlgoTimespan = (pindexLast->nHeight < params.MultiAlgoTimespanForkHeight) ? params.nMultiAlgoTimespan : params.nMultiAlgoTimespanV2;
    int64_t nMultiAlgoTargetSpacing = params.nMultiAlgoNum * nMultiAlgoTimespan; // 5 * 180(36) seconds = 900 seconds

    const CBlockIndex* pindexPrevAlgo = GetLastBlockIndexForAlgo(pindexLast, params, algo, nMultiAlgoTargetSpacing);
    if (pindexPrevAlgo == NULL || pindexFirst == NULL) {
        return nProofOfWorkLimit;
    }

    // Limit adjustment step
    // Use medians to prevent time-warp attacks
    int64_t nActualTimespan = pindexLast->GetMedianTimePast() - pindexFirst->GetMedianTimePast();
    // nAveragingTargetTimespan was initially not initialized correctly (heh)
    int64_t nMultiAlgoAveragingTargetTimespan;
    if (pindexLast->nHeight < params.DifficultyChangeForkHeight) {
        if (pindexLast->nHeight >= 222524) {
            nActualTimespan = nMultiAlgoAveragingTargetTimespan + (nActualTimespan - nMultiAlgoAveragingTargetTimespan) / 4;
            nMultiAlgoAveragingTargetTimespan = params.nMultiAlgoAveragingInterval * nMultiAlgoTargetSpacing; // 60* 5 * 180 = 54000 seconds
        } else {
            nMultiAlgoAveragingTargetTimespan = params.nMultiAlgoAveragingInterval * nMultiAlgoTargetSpacing; // 60* 5 * 180 = 54000 seconds
            nActualTimespan = nMultiAlgoAveragingTargetTimespan + (nActualTimespan - nMultiAlgoAveragingTargetTimespan) / 4;
        }
    } else {
        nMultiAlgoAveragingTargetTimespan = params.nMultiAlgoAveragingInterval * nMultiAlgoTargetSpacing; // 2* 5 * 180 = 1800 seconds
        nActualTimespan = nMultiAlgoAveragingTargetTimespan + (nActualTimespan - nMultiAlgoAveragingTargetTimespan) / 4;
    }

    int64_t nMinActualTimespan = nMultiAlgoAveragingTargetTimespan * (100 - (fDiffChange ? params.nMultiAlgoMaxAdjustUpV2   : params.nMultiAlgoMaxAdjustUp)) / 100;
    int64_t nMaxActualTimespan = nMultiAlgoAveragingTargetTimespan * (100 + (fDiffChange ? params.nMultiAlgoMaxAdjustDownV2 : params.nMultiAlgoMaxAdjustDown)) / 100;

    if (nActualTimespan < nMinActualTimespan)
        nActualTimespan = nMinActualTimespan;
    if (nActualTimespan > nMaxActualTimespan)
        nActualTimespan = nMaxActualTimespan;


    //Global retarget
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexPrevAlgo->nBits);

    // Litecoin: intermediate uint256 can overflow by 1 bit
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    bool fShift = bnNew.bits() > bnPowLimit.bits() - 1;
    if (fShift)
        bnNew >>= 1;

    bnNew *= nActualTimespan;
    bnNew /= nMultiAlgoAveragingTargetTimespan;

    //Per-algo retarget
    int nAdjustments = pindexPrevAlgo->nHeight + NUM_ALGOS - 1 - pindexLast->nHeight;
    if (nAdjustments > 0) {
        for (int i = 0; i < nAdjustments; i++) {
            bnNew *= 100;
            bnNew /= (100 + params.nMultiAlgoLocalTargetAdjustment);
        }
    } else if (nAdjustments < 0) { //make it easier
        for (int i = 0; i < -nAdjustments; i++) {
            bnNew *= (100 + params.nMultiAlgoLocalTargetAdjustment);
            bnNew /= 100;
        }
    }

    if (fShift)
        bnNew <<= 1;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // [smly] The timespan was changed at block 97050
    int64_t nPowTargetTimespan = params.nPowTargetTimespan;
    if (pindexLast->nHeight < params.FirstTimespanChangeHeight)
        nPowTargetTimespan = params.nPowOriginalTargetTimespan;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < nPowTargetTimespan/4)
        nActualTimespan = nPowTargetTimespan/4;
    if (nActualTimespan > nPowTargetTimespan*4)
        nActualTimespan = nPowTargetTimespan*4;

    // Retarget
    arith_uint256 bnNew;
    arith_uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    // Litecoin: intermediate uint256 can overflow by 1 bit
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    bool fShift = bnNew.bits() > bnPowLimit.bits() - 1;
    if (fShift)
        bnNew >>= 1;
    bnNew *= nActualTimespan;
    bnNew /= nPowTargetTimespan;
    if (fShift)
        bnNew <<= 1;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

const CBlockIndex* GetLastBlockIndexForAlgo(const CBlockIndex* pindex, const Consensus::Params& params, int algo, int64_t nMultiAlgoTargetSpacing)
{
    for (; pindex; pindex = pindex->pprev)
    {
        if (pindex->GetAlgo() != algo)
            continue;
        // Ignore special min-difficulty testnet blocks
        if (params.fPowAllowMinDifficultyBlocks &&
            pindex->pprev &&
            pindex->nTime > pindex->pprev->nTime + nMultiAlgoTargetSpacing * 2)
        {
            continue;
        }
        return pindex;
    }
    return nullptr;
}
