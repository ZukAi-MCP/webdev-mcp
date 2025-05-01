#!/usr/bin/env node

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js"
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js"
import { z } from "zod"
import { getAvailableScreens, takeScreenshot } from "./take-screenshot.js"

// Create server instance
const server = new McpServer({
	name: "webdev-mcp",
	version: "0.1"
})

// Register a tool for listing available screens
server.tool(
	"listScreens",
	"List available screens/displays that can be captured",
	{},
	async () => {
		try {
			console.log("Listing available screens...")
			const screens = await getAvailableScreens()

			// Format the output for display
			const screenList = screens
				.map((screen) => `Screen ${screen.id}: ${screen.description}`)
				.join("\n")

			return {
				content: [
					{
						type: "text",
						text: `Available screens:\n${screenList}`
					}
				]
			}
		} catch (error: unknown) {
			console.error("Error listing screens:", error)
			const errorMessage =
				error instanceof Error ? error.message : String(error)
			return {
				content: [
					{
						type: "text",
						text: `Error listing screens: ${errorMessage}`
					}
				]
			}
		}
	}
)

// Register screenshot taking tool
server.tool(
	"takeScreenshot",
	"Take a screenshot of a specific screen and return it as a base64 encoded string.",
	{
		screenId: z
			.number()
			.optional()
			.describe(
				"ID of the screen to capture. Use listScreens to find available screens. Default is 1 (main screen)"
			),
		timeout: z
			.number()
			.optional()
			.describe(
				"Maximum time to wait in milliseconds (default: 0, no timeout)"
			)
	},
	async ({ screenId, timeout }) => {
		try {
			const targetScreenId = screenId !== undefined ? screenId : 1
			console.log(`Taking screenshot of screen ${targetScreenId}...`)

			// Call the screenshot function with options
			const screenshot = await takeScreenshot("", {
				screenId: targetScreenId,
				timeout
			})

			console.log("Screenshot captured successfully")

			return {
				content: [
					{
						type: "text",
						text: `Screenshot of screen ${targetScreenId} captured successfully`
					},
					{
						type: "image",
						data: screenshot,
						mimeType: "image/png"
					}
				]
			}
		} catch (error: unknown) {
			console.error("Error taking screenshot:", error)
			const errorMessage =
				error instanceof Error ? error.message : String(error)
			return {
				content: [
					{
						type: "text",
						text: `Error taking screenshot: ${errorMessage}`
					}
				]
			}
		}
	}
)

async function main() {
	console.log("Starting webdev-mcp server with screen capture capabilities")

	const transport = new StdioServerTransport()
	await server.connect(transport)
}

main().catch((error) => {
	console.error("Fatal error in main():", error)
	process.exit(1)
})
