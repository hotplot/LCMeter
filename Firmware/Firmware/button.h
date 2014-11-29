#ifndef BUTTON_H_
#define BUTTON_H_

#include <stdint.h>

typedef struct {
	uint8_t state, noisyState;
	uint8_t count;
} button_t;

void initButton(button_t *button);
uint8_t updateButton(button_t *button, uint8_t currentState);

#endif