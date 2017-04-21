/*
 *  Created on: 15.04.2017
 *      Author: andru
 *
 *      nRF24LE1 remote dimmer unit
 *      support AES encryption
 *
 *		based on great nRF24LE1 SDK https://github.com/DeanCording/nRF24LE1_SDK
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gpio.h"
#include "delay.h"
#include "memory.h"
#include "interrupt.h"
#include "timer0.h"

#include "main.h"
#include "radio.h"
#include "dimmer.h"
#include "crc8.h"

#define FIRMWARE		10		// FW VER 0.10
#define CMDTMOUT		18000	// server command wait time in ~20us intervals

#if DEBUG
#define EN_UART			1	// use UART for debugging
#define UARTTXPIN		GPIO_PIN_ID_P0_3		// P0.3 - UART TX
#define UARTRXPIN		GPIO_PIN_ID_P0_4		// P0.4	- UART RX
#else
#define EN_UART			0	// use UART for debugging
#endif

#if EN_LED
#define LEDPIN	GPIO_PIN_ID_P1_4		// P1.4 - LED
#endif

#define PWMPIN	GPIO_PIN_ID_P0_2		// P0.2 - PWM0
#define IFPPIN	GPIO_PIN_ID_P0_6		// P0.6 - GPINT1
#define OUTPIN	GPIO_PIN_ID_P1_2		// P1.2 - dimmer output

#if EN_SW
#define SWPIN	GPIO_PIN_ID_P1_3		// P1.3 - switch input
#endif

#define NVM_START_ADDRESS	MEMORY_FLASH_NV_STD_END_START_ADDRESS
#define ENVM_START_ADDRESS	MEMORY_FLASH_NV_EXT_END_START_ADDRESS
#define ENVM_PAGE_NUM		MEMORY_FLASH_NV_EXT_END_FIRST_PAGE_NUM

#if EN_UART
#include "uart.h"
#endif

#if EN_WDG
#include "watchdog.h"
#define WDGTIMEOUT	1	// watchdog timeout, S
#endif

CONFIG_T config;

// halt
static void halt(void) {
	while (1) {
#if EN_LED
		gpio_pin_val_complement(LEDPIN);
		delay_ms(250);
#endif
	}
}

// radio interrupt in radio.c
interrupt_isr_rfirq();

// T0 interrupt in dimmer.c
interrupt_isr_t0();

// IFP interrupt in dimmer.c
interrupt_isr_ifp();

#if EN_RTC
#include "rtc2.h"
static volatile uint16_t rtc_counter;

static interrupt_isr_rtc2() {
#if EN_LED
	gpio_pin_val_complement(LEDPIN);
#endif
	if (rtc_counter > 0) rtc_counter--;
}
#endif

// write NVM config to eNVM
static uint8_t write_config(void) {
	uint8_t ret = CRC8((uint8_t *) &config, sizeof(CONFIG_T) - 1);
	config.crcbyte = ret;
	if (memory_flash_erase_page(ENVM_PAGE_NUM) != MEMORY_FLASH_OK)
		return 0;
	if (memory_flash_write_bytes(ENVM_START_ADDRESS, sizeof(CONFIG_T), (uint8_t *) &config) != MEMORY_FLASH_OK)
		return 0;
	return 1;
}

static void read_config(uint16_t addr) {
	uint16_t i;
	memory_movx_accesses_data_memory();
	for (i = 0; i < sizeof(CONFIG_T); i++) {
		*((uint8_t*) &config + i) = *((__xdata uint8_t*) addr + i);
	}
}

static void send_config(uint8_t addr, uint16_t value) {
	MESSAGE_T message;

#if DEBUG
	printf("addr=%d, value=%d\r\n", addr, (uint16_t) value);
#endif
	message.msgType = SENSOR_INFO;
	message.deviceID = config.deviceID;
	message.firmware = FIRMWARE;
	message.address = addr;
	message.data.iValue = (uint16_t) value;
	rfsend(&message);
}

static void send_config_err(uint8_t addr, uint8_t errcode) {
	MESSAGE_T message;

#if DEBUG
	printf("addr=%d, config error=%d\r\n", addr, errcode);
#endif
	message.msgType = SENSOR_ERROR;
	message.deviceID = config.deviceID;
	message.firmware = FIRMWARE;
	message.address = addr;
	message.error = errcode;
	rfsend(&message);
}

void send_dimmer(uint8_t state, uint8_t error) {
	MESSAGE_T message;

#if DEBUG
	printf("dimmer state=%d, error=%d\r\n", state, error);
#endif
	message.deviceID = config.deviceID;
	message.firmware = FIRMWARE;
	message.address = ADDR_DIMMER;
	message.msgType = SENSOR_INFO;
	message.data.iValue = state;
	rfsend(&message);

	if (error) {
		message.msgType = SENSOR_ERROR;
		message.error = error;
		rfsend(&message);
	}
}


// main
void main(void) {

	// variable definition
	uint8_t ret, cmd;
	uint16_t value;
    MESSAGE_T message;

	// program code
#if EN_LED
	gpio_pin_configure(LEDPIN,
		GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT
		| GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_SET
		| GPIO_PIN_CONFIG_OPTION_PIN_MODE_OUTPUT_BUFFER_NORMAL_DRIVE_STRENGTH
		);
#endif

#if EN_UART
	// Setup UART pins
	gpio_pin_configure(GPIO_PIN_ID_FUNC_RXD,
		GPIO_PIN_CONFIG_OPTION_DIR_INPUT |
		GPIO_PIN_CONFIG_OPTION_PIN_MODE_INPUT_BUFFER_ON_NO_RESISTORS
		);

	gpio_pin_configure(GPIO_PIN_ID_FUNC_TXD,
		GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT |
		GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_SET |
		GPIO_PIN_CONFIG_OPTION_PIN_MODE_OUTPUT_BUFFER_NORMAL_DRIVE_STRENGTH
		);

	uart_configure_8_n_1_38400();
#endif

	read_config(NVM_START_ADDRESS);
	ret = CRC8((uint8_t *) &config, sizeof(CONFIG_T)-1);
	if (config.crcbyte != ret) {
		// NVM config wrong stop work
		halt();
	}

	value = config.version;
	read_config(ENVM_START_ADDRESS);
	ret = CRC8((uint8_t *) &config, sizeof(CONFIG_T)-1);
	if (config.crcbyte != ret || config.version != value) {
		read_config(NVM_START_ADDRESS);
		if (!write_config()) {
			// config write error stop work
			halt();
		}
	}

#if EN_SW
	// SWPIN pin configure
	gpio_pin_configure(SWPIN,
			GPIO_PIN_CONFIG_OPTION_DIR_INPUT
			| GPIO_PIN_CONFIG_OPTION_PIN_MODE_INPUT_BUFFER_ON_NO_RESISTORS
	);
#endif

#if EN_WDG
	watchdog_setup();
	watchdog_set_wdsv_count(watchdog_calc_timeout_from_sec(WDGTIMEOUT));
#endif

#if EN_RF
 	radio_init();
#endif

#if EN_RTC
 	if (config.report > 0) {
		rtc_counter = config.report;
		//CLKLF is not running, so enable RCOSC32K and wait until it is ready
		pwr_clk_mgmt_clklf_configure(PWR_CLK_MGMT_CLKLF_CONFIG_OPTION_CLK_SRC_RCOSC32K);
		pwr_clk_mgmt_wait_until_clklf_is_ready();

		rtc2_configure(
			RTC2_CONFIG_OPTION_ENABLE
			| RTC2_CONFIG_OPTION_COMPARE_MODE_0_RESET_AT_IRQ,
			32767
		);			// 1s
		interrupt_control_rtc2_enable();
		interrupt_control_global_enable();
		rtc2_run();
 	}
#endif

	dimmer_init();

	if (config.state) {
		if (config.percent >= 10 && config.percent <= 100) {
			dimmer_run(config.percent);
			send_dimmer(config.percent, DIMMER_OK);
		}
	}

	while(1) {

		message.deviceID = config.deviceID;
		message.firmware = FIRMWARE;

#if EN_RTC
		if (config.report > 0 && rtc_counter == 0) {
			if (dimmer_state())
				send_dimmer(config.percent, DIMMER_OK);
			else
				send_dimmer(0, DIMMER_OK);
			rtc_counter = config.report;
		}
#endif

#if EN_SW
		if (gpio_pin_val_read(SWPIN)) {
			if (!dimmer_state() && config.percent >= 10 && config.percent <= 100) {
				dimmer_run(config.percent);
				send_dimmer(config.percent, DIMMER_OK);
			};
		} else {
			if (dimmer_state()) {
				dimmer_stop();
				send_dimmer(0, DIMMER_OK);
			}
		}
#endif

WAITCMD:
		// check receive command from smarthome gateway
		cmd = 0;
		if (rx_count--) {
			cmd = rfreadqueue(&message);
		}

		if (cmd && message.deviceID == config.deviceID && message.msgType == SENSOR_CMD) {
#if DEBUG
			printf("\r\ncommand: %d\r\n", message.command);
			printf("address: %d\r\n", message.address);
			printf("param: %d\r\n\r\n", (uint16_t) message.data.iValue);
#endif
			// команда управления диммером
			if (message.address == ADDR_DIMMER) {
				switch (message.command) {
				case CMD_ON:
					if (config.percent >= 10 && config.percent <= 100) {
						dimmer_run(config.percent);
						send_dimmer(config.percent, DIMMER_OK);
					} else {
						send_dimmer(0, DIMMER_PARAM);
					}
					break;
				case CMD_OFF:
					dimmer_stop();
					send_dimmer(0, DIMMER_OK);
					break;
				case CMD_ONTM:
					if (message.data.iValue >= 10 && message.data.iValue <= 100) {
						config.percent = message.data.iValue;
						dimmer_run(message.data.iValue);
						send_dimmer(message.data.iValue, DIMMER_OK);
					} else {
						send_dimmer(0, DIMMER_PARAM);
					}
					break;
				default:
					break;
				}
			// команда чтения/записи конфигурации
			} else if ((message.command == CMD_CFGREAD || message.command == CMD_CFGWRITE)) {
				switch (message.address) {
				case CFG_MAXSEND:
					if (message.command == CMD_CFGREAD) {
						send_config(CFG_MAXSEND, config.maxsend);
					} else {
						if (message.data.iValue == 0 || message.data.iValue > 50) {
						    send_config_err(CFG_MAXSEND, CFGSET_PARAM);
						    break;
						}
						config.maxsend = (uint8_t) message.data.iValue;
						if (!write_config())	{
							send_config_err(CFG_MAXSEND, CFGSET_WRITE);
							break;
						}
						send_config(CFG_MAXSEND, config.maxsend);
					}
					break;
				case CFG_STATE:
					if (message.command == CMD_CFGREAD) {
						send_config(CFG_STATE, config.state);
					} else {
						if (message.data.iValue > 1) {
						    send_config_err(CFG_STATE, CFGSET_PARAM);
						    break;
						}
						config.state = (uint8_t) message.data.iValue;
						if (!write_config())	{
							send_config_err(CFG_STATE, CFGSET_WRITE);
							break;
						}
						send_config(CFG_STATE, config.state);
					}
					break;
				case CFG_PERCENT:
					if (message.command == CMD_CFGREAD) {
						send_config(CFG_PERCENT, config.percent);
					} else {
						if (message.data.iValue < 10 || message.data.iValue > 100) {
						    send_config_err(CFG_PERCENT, CFGSET_PARAM);
						    break;
						}
						config.percent = (uint8_t) message.data.iValue;
						if (!write_config())	{
							send_config_err(CFG_PERCENT, CFGSET_WRITE);
							break;
						}
						send_config(CFG_PERCENT, config.percent);
					}
					break;
				case CFG_REPORT:
					if (message.command == CMD_CFGREAD) {
						send_config(CFG_REPORT, config.report);
					} else {
						if (message.data.iValue > 65000) {
						    send_config_err(CFG_REPORT, CFGSET_PARAM);
						    break;
						}
						config.report = (uint16_t) message.data.iValue;
						if (!write_config())	{
							send_config_err(CFG_REPORT, CFGSET_WRITE);
							break;
						}
						send_config(CFG_REPORT, config.report);
					}
					break;
				default:
					break;
				}
			}
			goto WAITCMD;
		}

		watchdog_set_wdsv_count(watchdog_calc_timeout_from_sec(WDGTIMEOUT));
//		delay_s(5);

	} // main loop
}
