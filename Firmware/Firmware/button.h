#ifndef BUTTON_H_
#define BUTTON_H_

#include <stdint.h>

typedef struct {
	uint8_t state;		// Stores the most recent stable state of the button
	uint8_t inputState;	// Stores the current, instantaneous reported state of the button input
	uint8_t count;		// Tracks how long the instantaneous state has remained unchanged
} button_t;

void initButton(button_t *button);

// Updates the specified button state based on the current input state.
// If the input state has been stable for long enough then the button stable state is changed.
// Returns true if the button state has changed, false if not.
uint8_t updateButton(button_t *button, uint8_t inputState);

#endif