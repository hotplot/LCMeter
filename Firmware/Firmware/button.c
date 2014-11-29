#include "button.h"

void initButton(button_t *button) {
	button->state = button->noisyState = 0;
	button->count = 0;
}

uint8_t updateButton(button_t *button, uint8_t currentState) {
	if (button->noisyState != currentState) {
		button->noisyState = currentState;
		button->count = 0;
	}
	
	if (button->count < 3)
		++button->count;
	
	if (button->count == 2 && button->state != button->noisyState) {
		button->state = button->noisyState;
		return 1;
	}
	
	return 0;
}
