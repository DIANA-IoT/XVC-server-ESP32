// Project Title: XVC_SERVER_ESP32
// File: main/bb_protocol.c
// Author: José-Borja Castillo-Sánchez
// Date: 2024
// (c) Copyright by Universidad de Málaga
// License This program is free software, you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "soc/gpio_struct.h"
#include "freertos/stream_buffer.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"

#define PORT CONFIG_EXAMPLE_PORT

#define TCP_PROCESS_TASK_PRIO 6 //TCP task priority

#if defined(__riscv)
#define ESP32_C3 1
//#else if defined(_) __xtensa__ defined
#endif
#if ESP32_C3
#define SINGLE_CORE 1
#endif

#define DEBUG_PIN 27

#ifndef SINGLE_CORE //ESP32 pins
#define TCK_PIN 13 	//ORIG: 13
#define TMS_PIN 14	//ORIG: 14
#define TDI_PIN 15	//ORIG: 12
#define TDO_PIN 12	//ORIG: 15
#define LOWER_PIN TMS_PIN
#define TCK_TMS_TDI_MASK ((1<<TCK_PIN)|(1<<TMS_PIN)|(1<<TDI_PIN))

#else //ESP32_C3 Pins
#define TCK_PIN 6	//ORIG: 6
#define TMS_PIN 4	//ORIG: 4
#define TDI_PIN 5	//ORIG: 5
#define TDO_PIN 7	//ORIG: 7
#define LOWER_PIN TMS_PIN
#define TCK_TMS_TDI_MASK ((1<<TCK_PIN)|(1<<TMS_PIN)|(1<<TDI_PIN))
#endif


static const char *TAG = "TCPSERVER"; //Task name
static int sock;
static TaskHandle_t tcp_task_handle;
StreamBufferHandle_t xStreamBuffer;
EventGroupHandle_t evg;

#define BUF_LENGTH 2048
#define SB_SIZE 8192

static uint8_t tms_buf[BUF_LENGTH];
static uint8_t tdi_buf[BUF_LENGTH];
static uint8_t tdo_buf[BUF_LENGTH];

void xvc_GPIOConfigure(void)
{
	gpio_config_t gpio_config_st = { };
	gpio_config_st.pin_bit_mask = (1ULL << TCK_PIN) | (1ULL << TMS_PIN) | (1ULL << TDI_PIN);
	gpio_config_st.mode = GPIO_MODE_OUTPUT;
	gpio_config_st.pull_up_en = GPIO_PULLUP_ENABLE;
	gpio_config_st.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_config_st.intr_type = GPIO_INTR_DISABLE;
	//From Rpi example.....
	//Sets a known state on TCK, TMS, TDI (0,1,0)
#if ESP32_C3
	GPIO.out_w1tc.val = (1 << TCK_PIN) | (1 << TDI_PIN);
	GPIO.out_w1ts.val = (1 << TMS_PIN);
#else
	GPIO.out_w1tc = (1 << TCK_PIN) | (1 << TDI_PIN);
	GPIO.out_w1ts = (1 << TMS_PIN);
#endif

	gpio_config(&gpio_config_st);

	gpio_config_st.pin_bit_mask = (1ULL << TDO_PIN);
	gpio_config_st.mode = GPIO_MODE_INPUT;
	gpio_config_st.pull_up_en = GPIO_PULLUP_ENABLE;

	gpio_config(&gpio_config_st);

	//From Rpi example.....
	//Sets a known state on TCK, TMS, TDI (0,1,0)
#if ESP32_C3
	GPIO.out_w1tc.val = (1 << TCK_PIN) | (1 << TDI_PIN);
	GPIO.out_w1ts.val = (1 << TMS_PIN);
#else
	GPIO.out_w1tc = (1 << TCK_PIN) | (1 << TDI_PIN);
	GPIO.out_w1ts = (1 << TMS_PIN);
#endif
}

static int16_t read_frame(int SockID, void *ptr, int32_t size)
{
	int32_t state, i;

	i = size;
	while (i > 0) {
		state = read(SockID, ptr, i);
		if (state > 0) {
			ptr += state;
			i -= state;
		} else {
			return state;
		}
	}
	return size;
}

/* This is a MACRO, so index and index-derived values will still be constants */
//This macro is just to avoid silly copy-paste-modify mistakes and make the code more readable
#if ESP32_C3
#define XVC_SHIFT_BIT(index) {\
		/*temp is not what you would call a beauty, but it gets the job done.*/\
		temp = ( ( (tms >> index) & 0x01) << TMS_PIN) | ( ( (tdi >> index) & 0x01) << TDI_PIN); /*TCK 0*/ \
		GPIO.out_w1tc.val = (temp ^ TCK_TMS_TDI_MASK); /*XOR with TCK_TMS_TDI_MASK to clear the bits with 0 value (including TCK)*/\
		GPIO.out_w1ts.val =  temp; /*Set the bits with 1 value*/ \
		GPIO.out_w1ts.val =  (1 << TCK_PIN); /* Sets a rising edge on TCK. */ \
		tdo |= ((GPIO.in.val >> (TDO_PIN-index)) & (0x01<<index)); /*read TDO. */ \
	}
#else
#define XVC_SHIFT_BIT(index) {\
		temp =  ( (tms << (TMS_PIN - index)) & (1 << TMS_PIN) ) | ( (tdi <<(TDI_PIN - index)) & (1 << TDI_PIN) ); /*TCK 0*/ \
		GPIO.out_w1tc = (temp ^ TCK_TMS_TDI_MASK); /*XOR with TCK_TMS_TDI_MASK to clear the bits with 0 value (including TCK)*/\
		GPIO.out_w1ts =  temp; /*Set the bits with 1 value*/ \
		GPIO.out_w1ts =  (1 << TCK_PIN); /* Sets a rising edge on TCK. */ \
		tdo |= ((GPIO.in >> (TDO_PIN-index)) & (0x01<<index)); /*read TDO. */ \
	}
#endif

static void IRAM_ATTR xvc_shift(uint32_t number_of_bits)
{
	//From Rpi Example
	//Sets a known state on TCK, TMS, TDI (0,1,1)
#if ESP32_C3
	GPIO.out_w1tc.val = (1 << TCK_PIN) | (1 << TDI_PIN);
	GPIO.out_w1ts.val = (1 << TMS_PIN);
#else
	GPIO.out_w1tc = (1 << TCK_PIN) | (1 << TDI_PIN);
	GPIO.out_w1ts = (1 << TMS_PIN);
#endif

	uint8_t remainder = number_of_bits % 8;

	register uint16_t operations_cnt = number_of_bits / 8;
	register uint8_t *tms_buf_ptr = tms_buf;
	register uint8_t *tdi_buf_ptr = tdi_buf;
	register uint8_t *tdo_buf_ptr = tdo_buf;
	register uint32_t tdi; //holds tdi bytes. 32 bits to avoid > 8 shift problems
	register uint32_t tms; //holds tms bytes. 32 bits to avoid > 8 shift problems
	register uint32_t temp; //Temporary values to send to gpio.out (via out_w1ts and out_w1tc)
	register uint8_t tdo = 0;

	while (operations_cnt) {
		/*		
		 * Unrolls the loop, with that, we aim to extract the last bit of performance.
		 */
		tdo = 0;
		tdi = *tdi_buf_ptr; //read TDI byte to register
		tms = *tms_buf_ptr; //read TMS byte to register

		//SHIFT bits
		XVC_SHIFT_BIT(0);
		XVC_SHIFT_BIT(1);
		XVC_SHIFT_BIT(2);
		XVC_SHIFT_BIT(3);
		XVC_SHIFT_BIT(4);
		XVC_SHIFT_BIT(5);
		XVC_SHIFT_BIT(6);
		XVC_SHIFT_BIT(7);

		operations_cnt--;
		tms_buf_ptr++;
		tdi_buf_ptr++;
		*tdo_buf_ptr = tdo;
		tdo_buf_ptr++;
	}

	if (remainder) {
		//Shift remaining bits of the last byte.
		tdo = 0;
		tdi = *tdi_buf_ptr; //read TDI byte to register
		tms = *tms_buf_ptr; //read TMS byte to register

		//Can't optimize too much. Order has to be preserved
		switch (remainder) {
		case 1:
			XVC_SHIFT_BIT(0)
			;
			break;
		case 2:
			XVC_SHIFT_BIT(0)
			;
			XVC_SHIFT_BIT(1)
			;
			break;
		case 3:
			XVC_SHIFT_BIT(0)
			XVC_SHIFT_BIT(1)
			XVC_SHIFT_BIT(2)
			break;
		case 4:
			XVC_SHIFT_BIT(0)
			XVC_SHIFT_BIT(1)
			XVC_SHIFT_BIT(2)
			XVC_SHIFT_BIT(3)
			break;
		case 5:
			XVC_SHIFT_BIT(0)
			XVC_SHIFT_BIT(1)
			XVC_SHIFT_BIT(2)
			XVC_SHIFT_BIT(3)
			XVC_SHIFT_BIT(4)
			break;
		case 6:
			XVC_SHIFT_BIT(0)
			XVC_SHIFT_BIT(1)
			XVC_SHIFT_BIT(2)
			XVC_SHIFT_BIT(3)
			XVC_SHIFT_BIT(4)
			XVC_SHIFT_BIT(5)
			break;
		case 7:
			XVC_SHIFT_BIT(0)
			XVC_SHIFT_BIT(1)
			XVC_SHIFT_BIT(2)
			XVC_SHIFT_BIT(3)
			XVC_SHIFT_BIT(4)
			XVC_SHIFT_BIT(5)
			XVC_SHIFT_BIT(6)
			break;
		}

		*tdo_buf_ptr = tdo;
		tdo_buf_ptr++;
	}

	//From Rpi example.....
	//Sets a known state on TCK, TMS, TDI (0,1,0)
#if ESP32_C3
	GPIO.out_w1tc.val = (1 << TCK_PIN) | (1 << TDI_PIN);
	GPIO.out_w1ts.val = (1 << TMS_PIN);
#else
	GPIO.out_w1tc = (1 << TCK_PIN) | (1 << TDI_PIN);
	GPIO.out_w1ts = (1 << TMS_PIN);
#endif

}

static int tcp_server_process_data(const int sock)
{
	static const char XVC_version[] = "xvcServer_v1.0:4096\n"; //Maximum buffer length = 4 KB.
	uint32_t number_of_bits;
	size_t sent;
	int err;
	uint8_t rxBuffer[20];
	uint8_t txBuffer[20];

	while (1) {
		err = read(sock, &rxBuffer, 2);
		if (err <= 0) {
			ESP_LOGE(TAG, "Error reading frame....\n");
			return err;
		}
		if (memcmp(rxBuffer, "ge", 2) == 0) {
			err = read_frame(sock, rxBuffer, 6);
			if (err <= 0) {
				ESP_LOGE(TAG, "Error reading frame....\n");
				return err;
			}
			//memcpy(txBuffer, XVC_version, 20);
			sent = send(sock, XVC_version, strlen(XVC_version), 0);
			if (sent != strlen(XVC_version)) {
				ESP_LOGE(TAG, "Error, XVC version not sent....\n");
				return -1;
			}
		} else if (memcmp(rxBuffer, "se", 2) == 0) {
			err = read_frame(sock, rxBuffer, 9);
			if (err <= 0) {
				ESP_LOGE(TAG, "Error reading frame....\n");
				return err;
			}
			memcpy(txBuffer, rxBuffer + 5, 4);
			sent = send(sock, txBuffer, 4, 0);
			if (sent != 4) {
				ESP_LOGE(TAG, "Error, XVC reply to settck....\n");
				return -1;
			}
		} else if (memcmp(rxBuffer, "sh", 2) == 0) {
			err = read_frame(sock, rxBuffer, 8);
			if (err <= 0) {
				ESP_LOGE(TAG, "Error reading frame....\n");
				return err;
			}
			memcpy(&number_of_bits, rxBuffer + 4, 4);
			uint16_t number_of_bytes;
			number_of_bytes = (7 + number_of_bits) / 8;
			err = read_frame(sock, tms_buf, number_of_bytes);
			if (err <= 0) {
				ESP_LOGE(TAG, "Error reading tms vector....\n");
				return err;
			}
			err = read_frame(sock, tdi_buf, number_of_bytes);
			if (err <= 0) {
				ESP_LOGE(TAG, "Error reading tdi vector....\n");
				return err;
			}
			if (number_of_bytes > sizeof(tms_buf)) {
				ESP_LOGE(TAG, "Error, payload does not fit....\n");
				return -1;
			}
			xvc_shift(number_of_bits);
			//Send TDO vector.
			err = send(sock, tdo_buf, ((7 + number_of_bits) / 8), 0);
			if (err <= 0) {
				ESP_LOGE(TAG, "Error sending TDO frame....\n");
				return err;
			}
		} else {
			ESP_LOGI(TAG, "Unrecognized command\n");
			return -1;
		}
	}
	return 0;
}

static void tcp_server_task(void *pvParameters)
{
	char addr_str[128];
	int addr_family;
	int ip_protocol;

#ifdef CONFIG_EXAMPLE_IPV4

	struct sockaddr_in dest_addr;
	dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	dest_addr.sin_family = AF_INET;
	dest_addr.sin_port = htons(PORT);
	addr_family = AF_INET;
	ip_protocol = IPPROTO_IP;
	inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
#else // IPV6
    struct sockaddr_in6 dest_addr;
    bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
    dest_addr.sin6_family = AF_INET6;
    dest_addr.sin6_port = htons(PORT);
    addr_family = AF_INET6;
    ip_protocol = IPPROTO_IPV6;
    inet6_ntoa_r(dest_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
#endif

	int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
	if (listen_sock < 0) {
		ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
		vTaskDelete(NULL);
		return;
	}
	ESP_LOGI(TAG, "Socket created");

	int err = bind(listen_sock, (struct sockaddr*) &dest_addr, sizeof(dest_addr));
	if (err != 0) {
		ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
		goto CLEAN_UP;
	}
	ESP_LOGI(TAG, "Socket bound, port %d", PORT);

	err = listen(listen_sock, 1);
	if (err != 0) {
		ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
		goto CLEAN_UP;
	}
	while (1) {

		ESP_LOGI(TAG, "Socket listening");
		struct sockaddr_in source_addr; // Large enough for both IPv4 or IPv6
		uint addr_len = sizeof(source_addr);
		sock = accept(listen_sock, (struct sockaddr*) &source_addr, &addr_len);
		if (sock < 0) {
			ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
			break;
		}

		/*
		 Deactivates Nagle's Algorithm
		 */
		int flag = 1;
		if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*) &flag, sizeof(int)) < 0)
			ESP_LOGE(TAG, "Set sockopt %d", errno);

		// Convert ip address to string
		if (source_addr.sin_family == PF_INET) {
			inet_ntoa_r(((struct sockaddr_in* )&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
		} else {
			ESP_LOGE(TAG, "IPv6 DEACTIVATED");
		}
		ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

		tcp_server_process_data(sock);

		shutdown(sock, 0);
		close(sock);
	}

	CLEAN_UP: close(listen_sock);
	vTaskDelete(NULL);
}

void initTCPServer(void)
{
#ifndef SINGLE_CORE
	xTaskCreatePinnedToCore(tcp_server_task, "tcp_server", 8192, NULL, TCP_PROCESS_TASK_PRIO, &tcp_task_handle, 1); //Creates the task "tcp_server_task"
#else
	xTaskCreate(tcp_server_task, "tcp_server", 8192, NULL, TCP_PROCESS_TASK_PRIO, &tcp_task_handle);
	#endif
}

