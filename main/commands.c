// Project Title: XVC_SERVER_ESP32
// File: main/commands.c
// Author: José-Borja Castillo-Sánchez
// Date: 2024
// (c) Copyright by Universidad de Málaga
// License This program is free software, you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_spi_flash.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "argtable3/argtable3.h"
#include "commands.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "wifi.h"

//WiFi command
static int Cmd_wifi(int argc, char **argv)
{
	if (argc != 2)
	{
		//Displays usage
		printf(" wifi [AP|STA]\r\n");
	}
	else
	{
		if (0==strncmp( argv[1], "AP",2))
		{
			printf("WiFi Access Point mode\r\n");
		    wifi_change_to_AP();
		}
		else if (0==strncmp( argv[1], "STA",3))
		{
			printf("WiFi Station\r\n");
			wifi_change_to_sta();
		}
		else
		{
			printf(" wifi [AP|STA]\r\n");
		}
	}
    return 0;
}

static void register_Cmd_wifi(void)
{
    const esp_console_cmd_t cmd = {
        .command = "wifi",
        .help = "Sets WiFi mode",
        .hint = NULL,
        .func = &Cmd_wifi,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

void init_Commands(void)
{
	register_Cmd_wifi();
}
