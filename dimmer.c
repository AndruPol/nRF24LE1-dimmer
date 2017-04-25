/*
 * dimmer.c
 *
 *  Created on: 15.04.2017
 *      Author: andru
 */

#define OUTPIN		GPIO_PIN_ID_P1_2		// P1.2 - dimmer output
#define T0PIN		GPIO_PIN_ID_P0_7		// P0.7 - T0
#define IFPPIN		GPIO_PIN_ID_P0_6		// P0.6 - GPINT1

#define PULSE190US		0xFFFE		// ~190uS high pulse

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gpio.h"
#include "pwm.h"
#include "timer0.h"
#include "interrupt.h"

#include "dimmer.h"
#include "main.h"

static volatile uint16_t ch_delay;
static volatile uint8_t ch_on = 0, ch_pulse;

// timer0 overflow isr
interrupt_isr_t0() {
	timer0_stop();
	if (! ch_on) return;
	if (! ch_pulse) {
		gpio_pin_val_set(OUTPIN);
		ch_pulse = 1;
		timer0_set_t0_val(PULSE190US);
		timer0_run();
	} else {
		gpio_pin_val_clear(OUTPIN);
		ch_pulse = 0;
	}
#if 0
	printf("t0 isr fired\r\n");
#endif
}

// zero cross detector isr
interrupt_isr_ifp() {
	gpio_pin_val_clear(OUTPIN);
	timer0_stop();
	if (ch_on) {
		timer0_set_t0_val(ch_delay);
		timer0_run();
	}
#if 0
	printf("ifp isr fired\r\n");
#endif
}

// init dimmer
void  dimmer_init(void) {
	// GPINT1 pin configure
	gpio_pin_configure(IFPPIN,
			GPIO_PIN_CONFIG_OPTION_DIR_INPUT
			| GPIO_PIN_CONFIG_OPTION_PIN_MODE_INPUT_BUFFER_ON_NO_RESISTORS
	);

	interrupt_configure_ifp(
			INTERRUPT_IFP_INPUT_GPINT1,
			INTERRUPT_IFP_CONFIG_OPTION_ENABLE
			| INTERRUPT_IFP_CONFIG_OPTION_TYPE_FALLING_EDGE
	);

	// T0 pin
	gpio_pin_configure(T0PIN,
			GPIO_PIN_CONFIG_OPTION_DIR_INPUT
			| GPIO_PIN_CONFIG_OPTION_PIN_MODE_INPUT_BUFFER_ON_NO_RESISTORS
	);

	// dimmer output pin
	gpio_pin_configure(OUTPIN,
			GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT
			| GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_CLEAR
			| GPIO_PIN_CONFIG_OPTION_PIN_MODE_OUTPUT_BUFFER_NORMAL_DRIVE_STRENGTH
	);

	// 10457.5 Hz
	pwm_configure(
			PWM_CONFIG_OPTION_PRESCALER_VAL_5
			| PWM_CONFIG_OPTION_WIDTH_8_BITS
	);

	// ~50uS high pulse if 134
	pwm_start(
			PWM_CHANNEL_0,
			128
	);

	timer0_configure(
			TIMER0_CONFIG_OPTION_MODE_1_16_BIT_CTR_TMR
			| TIMER0_CONFIG_OPTION_FUNCTION_COUNT_EVENTS_ON_T0
			| TIMER0_CONFIG_OPTION_GATE_ALWAYS_RUN_TIMER,
			0
	);

	ch_pulse = 0;
	ch_on = 0;

	interrupt_set_priority(
			INTERRUPT_PRIORITY_GROUP_IFP_RFRDY,
			INTERRUPT_PRIORITY_LEVEL_2
	);

	interrupt_set_priority(
			INTERRUPT_PRIORITY_GROUP_TF0_RFIRQ,
			INTERRUPT_PRIORITY_LEVEL_1
	);

	interrupt_control_t0_enable();
	interrupt_control_ifp_enable();
	interrupt_control_global_enable();
}

// start dimmer with percent value in range 20-100%
uint8_t dimmer_run(uint8_t percent) {
	if (percent < DIMMERMIN || percent > DIMMERMAX) return 0;
	if (ch_on) ch_on = 0;
	ch_delay = (uint16_t) 0xFF00 | (254 + (percent - 100));
	ch_pulse = 0;
	ch_on = 1;

#if DEBUG
	printf("ch_delay=0x%x \r\n", ch_delay);
#endif
	return 1;
}

// stop dimmer
void dimmer_stop(void) {
	ch_pulse = 0;
	ch_on = 0;
}

uint8_t dimmer_state(void) {
	return ch_on;
}
