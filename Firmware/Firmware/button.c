#include "button.h"

void initButton(button_t *button) {
	button->state = button->inputState = 0;
	button->count = 0;
}

uint8_t updateButton(button_t *button, uint8_t inputState) {
	// If the current input state does not match the previous input state, reset the counter and change the state
	if (button->inputState != inputState) {
		button->inputState = inputState;
		button->count = 0;
	}
	
	// Increment the counter 
	if (button->count < 3)
		++button->count;
	
	// Check whether the instantaneous state has been stable for long enough and the stable state needs to be updated
	if (button->count == 2 && button->state != button->inputState) {
		button->state = button->inputState;
		return 1;
	}
	
	return 0;
}
