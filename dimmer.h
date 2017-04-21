/*
 * dimmer.h
 *
 *  Created on: 15.04.2017
 *      Author: andru
 */

#ifndef DIMMER_H_
#define DIMMER_H_

void dimmer_init(void);
uint8_t dimmer_run(uint8_t percent);
void dimmer_stop(void);
uint8_t dimmer_state(void);

#endif /* DIMMER_H_ */
