// Project Title: XVC_SERVER_ESP32
// File: main/main.c
// Author: José-Borja Castillo-Sánchez
// Date: 2024
// (c) Copyright by Universidad de Málaga
// License This program is free software, you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "driver/uart_vfs.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_fat.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

#include "wifi.h"
#include "bb_protocol.h"
#include "cmd_system.h"
#include "commands.h"

#if CONFIG_STORE_HISTORY
#define MOUNT_PATH "/data"
#define HISTORY_PATH MOUNT_PATH "/history.txt"
#endif

// Initializes command interpreter
static void initialize_console(void)
{
	/* Drain stdout before reconfiguring it */
	fflush(stdout);
	fsync(fileno(stdout));

	/* Disable buffering on stdin */
	setvbuf(stdin, NULL, _IONBF, 0);

	/* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
	uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
	/* Move the caret to the beginning of the next line on '\n' */
	uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

	/* Configure UART. Note that REF_TICK is used so that the baud rate remains
	 * correct while APB frequency is changing in light sleep mode.
	 */
	const uart_config_t uart_config = {
		.baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.source_clk = UART_SCLK_REF_TICK, // ESP32 specific
										  // .source_clk = UART_SCLK_APB, //ESP32C3
	};
	/* Install UART driver for interrupt-driven reads and writes */
	ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
										256, 0, 0, NULL, 0));
	ESP_ERROR_CHECK(uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config));

	/* Tell VFS to use UART driver */
	uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

	/* Initialize the console */
	esp_console_config_t console_config = {
		.max_cmdline_args = 8,
		.max_cmdline_length = 256,
#if CONFIG_LOG_COLORS
		.hint_color = atoi(LOG_COLOR_CYAN)
#endif
	};
	ESP_ERROR_CHECK(esp_console_init(&console_config));

	/* Configure linenoise line completion library */
	/* Enable multiline editing. If not set, long commands will scroll within
	 * single line.
	 */
	linenoiseSetMultiLine(1);

	/* Tell linenoise where to get command completions and hints */
	linenoiseSetCompletionCallback(&esp_console_get_completion);
	linenoiseSetHintsCallback((linenoiseHintsCallback *)&esp_console_get_hint);

	/* Set command history size */
	linenoiseHistorySetMaxLen(100);

	/* Set command maximum length */
	linenoiseSetMaxLineLen(console_config.max_cmdline_length);

	/* Don't return empty lines. */
	linenoiseAllowEmpty(false);

#if CONFIG_STORE_HISTORY
	/* Load command history from filesystem */
	linenoiseHistoryLoad(HISTORY_PATH);
#endif
}

void app_main(void)
{
	// Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	// Initializes WiFi library,
	wifi_initlib();
	
#if CONFIG_EXAMPLE_CONNECT_WIFI_STATION
		wifi_init_sta();
#else
		wifi_init_softap();
#endif

	initialize_console();

	/* Register commands */
	esp_console_register_help_command();
	register_system_common();
	init_Commands();

	xvc_GPIOConfigure();
	initTCPServer();

	printf("\n"
		   "This is an example of ESP-IDF console component.\n"
		   "Type 'help' to get the list of commands.\n"
		   "Use UP/DOWN arrows to navigate through command history.\n"
		   "Press TAB when typing command name to auto-complete.\n");

	const char *prompt = LOG_COLOR_I "esp32> " LOG_RESET_COLOR;
	/* Figure out if the terminal supports escape sequences */
	int probe_status = linenoiseProbe();
	if (probe_status)
	{ /* zero indicates success */
		printf("\n"
			   "Your terminal application does not support escape sequences.\n"
			   "Line editing and history features are disabled.\n"
			   "On Windows, try using Putty instead.\n");
		linenoiseSetDumbMode(1);
#if CONFIG_LOG_COLORS
		/* Since the terminal doesn't support escape sequences,
		 * don't use color codes in the prompt.
		 */
		prompt = "esp32> ";
#endif // CONFIG_LOG_COLORS
	}

	/* Main loop (command interpreter)*/
	while (true)
	{
		/* Get a line using linenoise.
		 * The line is returned when ENTER is pressed.
		 */
		char *line = linenoise(prompt);
		if (line == NULL)
		{ /* Ignore empty lines */
			continue;
		}
		/* Add the command to the history */
		linenoiseHistoryAdd(line);
#if CONFIG_STORE_HISTORY
		/* Save command history to filesystem */
		linenoiseHistorySave(HISTORY_PATH);
#endif

		/* Try to run the command */
		int ret;
		esp_err_t err = esp_console_run(line, &ret);
		if (err == ESP_ERR_NOT_FOUND)
		{
			printf("Unrecognized command\n");
		}
		else if (err == ESP_ERR_INVALID_ARG)
		{
			// command was empty
		}
		else if (err == ESP_OK && ret != ESP_OK)
		{
			printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
		}
		else if (err != ESP_OK)
		{
			printf("Internal error: %s\n", esp_err_to_name(err));
		}
		/* linenoise allocates line buffer on the heap, so need to free it */
		linenoiseFree(line);
	}
}
