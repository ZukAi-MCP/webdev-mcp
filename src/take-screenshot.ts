/**
 * Module for taking screenshots of a specific screen
 */

import { exec } from "node:child_process"
import * as fs from "node:fs"
import * as os from "node:os"
import * as path from "node:path"
import { promisify } from "node:util"
import sharp from "sharp"

const execAsync = promisify(exec)

export async function takeScreenshot(
	url: string,
	options: {
		width?: number
		height?: number
		fullPage?: boolean
		waitForSelector?: string
		timeout?: number
		screenId?: number
	} = {}
): Promise<string> {
	try {
		const screenId = options.screenId !== undefined ? options.screenId : 1

		// Create a temporary file path
		const tmpDir = os.tmpdir()
		const screenshotPath = path.join(tmpDir, `screenshot-${Date.now()}.png`)

		// Determine which command to use based on OS
		const platform = os.platform()
		let cmd = ""

		if (platform === "darwin") {
			// Get display info first to check if the requested display exists
			const screens = await getAvailableScreens()
			const displayExists = screens.some(
				(screen) => screen.id === screenId
			)

			if (displayExists || screenId === 1) {
				// On macOS, use screencapture with -D flag to specify display ID
				cmd = `screencapture -D ${screenId} -x "${screenshotPath}"`
			} else {
				// Default to main display if the specified one doesn't exist
				console.warn(
					`Display ID ${screenId} not found, defaulting to main display`
				)
				cmd = `screencapture -x "${screenshotPath}"`
			}
		} else if (platform === "win32") {
			// Windows implementation - doesn't support multiple screens yet
			cmd = `powershell -command "Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.SendKeys]::SendWait('{PRTSC}'); Start-Sleep -Milliseconds 500; $img = [System.Windows.Forms.Clipboard]::GetImage(); $img.Save('${screenshotPath}')"`
		} else if (platform === "linux") {
			// Linux implementation - doesn't support multiple screens yet
			cmd = `import -window root "${screenshotPath}"`
		} else {
			throw new Error(`Unsupported platform: ${platform}`)
		}

		// Execute the command
		await execAsync(cmd, { timeout: options.timeout || 0 })

		// If screenId is 2, resize the image to 819x1456 px
		if (screenId === 2) {
			const resizedPath = path.join(
				tmpDir,
				`screenshot-resized-${Date.now()}.png`
			)

			await sharp(screenshotPath)
				.resize(819, 1456, {
					fit: "contain",
					background: { r: 255, g: 255, b: 255, alpha: 1 }
				})
				.toFile(resizedPath)

			// Delete the original screenshot and use the resized one
			await fs.promises.unlink(screenshotPath)

			// Read the resized screenshot file and convert to base64
			const screenshotData = await fs.promises.readFile(resizedPath)
			const base64Data = screenshotData.toString("base64")

			// Clean up the temporary file
			await fs.promises.unlink(resizedPath)

			return base64Data
		}

		// For other screenIds, continue with the original behavior
		const screenshotData = await fs.promises.readFile(screenshotPath)
		const base64Data = screenshotData.toString("base64")

		// Clean up the temporary file
		await fs.promises.unlink(screenshotPath)

		return base64Data
	} catch (error) {
		console.error("Error taking screenshot:", error)
		throw new Error(
			`Failed to capture screenshot: ${error instanceof Error ? error.message : String(error)}`
		)
	}
}

// Add a helper function to list available screens (macOS only)
export async function getAvailableScreens(): Promise<
	{ id: number; description: string }[]
> {
	try {
		const platform = os.platform()

		if (platform === "darwin") {
			// Use system_profiler to get display information
			const { stdout } = await execAsync(
				"system_profiler SPDisplaysDataType"
			)

			// Parse the system_profiler output to properly identify displays
			// First, separate the output into sections for each display
			const displayData = stdout
				.split("Graphics/Displays:")
				.filter((section) => section.trim().length > 0)

			if (displayData.length === 0) {
				// Fallback if no displays were found
				return [{ id: 1, description: "Main Display" }]
			}

			// Initial display list with Main Display
			const displays: { id: number; description: string }[] = []

			// Scan through raw output and extract display sections
			const displaySections = stdout
				.split(/^\s*\w+:/m)
				.filter(
					(section) =>
						section.includes("Display Type") ||
						section.includes("Resolution") ||
						section.includes("Type:")
				)

			// Check if we're dealing with a more specific format
			if (displaySections.length === 0) {
				// Fallback to main display only
				return [{ id: 1, description: "Main Display" }]
			}

			// Process each section to extract display info
			for (let i = 0; i < displaySections.length; i++) {
				const section = displaySections[i]
				const lines = section.split("\n").map((line) => line.trim())

				let displayName = ""
				let resolution = ""
				let displayType = ""

				// Extract relevant display information
				for (const line of lines) {
					if (
						line.includes("Display Type:") ||
						line.includes("Type:")
					) {
						displayType = line.split(":")[1]?.trim() || "Unknown"
					} else if (line.includes("Resolution:")) {
						resolution = line.split(":")[1]?.trim() || ""
					} else if (line.includes("Name:")) {
						displayName = line.split(":")[1]?.trim() || ""
					}
				}

				// Combine information for a descriptive name
				let description = displayName || displayType || "Display"
				if (resolution) {
					description += ` (${resolution})`
				}

				// Add to display list
				displays.push({
					id: i + 1, // Display IDs in screencapture start at 1
					description
				})
			}

			// If we found displays but none matched our parsing, add a fallback
			if (displays.length === 0) {
				displays.push({ id: 1, description: "Main Display" })
			}

			// Use an alternative method: Check output from "system_profiler SPDisplaysDataType"
			if (displays.length <= 1) {
				// Try running a different command to get screen info
				try {
					const { stdout: screenInfo } = await execAsync(
						"system_profiler SPDisplaysDataType | grep Resolution"
					)
					const resolutions = screenInfo
						.split("\n")
						.filter((line) => line.includes("Resolution"))

					// If we have multiple resolutions, we likely have multiple screens
					if (resolutions.length > 1) {
						displays.length = 0 // Clear existing displays

						// Add each detected display
						for (let i = 0; i < resolutions.length; i++) {
							const resolution =
								resolutions[i].split(":")[1]?.trim() || ""
							displays.push({
								id: i + 1,
								description:
									i === 0
										? `Main Display (${resolution})`
										: `External Display ${i} (${resolution})`
							})
						}
					}
				} catch (err) {
					console.error("Error getting alternative screen info:", err)
				}
			}

			return displays
		}

		if (platform === "win32") {
			// Windows implementation (placeholder)
			return [
				{
					id: 0,
					description:
						"Primary Screen (multi-screen selection not supported yet)"
				}
			]
		}

		if (platform === "linux") {
			// Linux implementation (placeholder)
			return [
				{
					id: 0,
					description:
						"Primary Screen (multi-screen selection not supported yet)"
				}
			]
		}

		throw new Error(`Unsupported platform: ${platform}`)
	} catch (error) {
		console.error("Error listing screens:", error)
		return [{ id: 1, description: "Main Display" }]
	}
}
