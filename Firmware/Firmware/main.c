#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <avr/io.h>
#include <avr/interrupt.h>

#define F_CPU 16000000UL
#include <util/delay.h>

#include "lcd/hd44780.h"
#include "button.h"

#define setBit(reg, bit, state) (state) ? (reg |= (1 << bit)) : (reg &= ~(1 << bit));

#define TWO_PI_SQR 39.4784176f

#define NUM_READINGS 10
static uint8_t  FreqReadingIndex = 0;
static uint16_t FreqReadings[NUM_READINGS];

static uint32_t CurrentFreq = 0;
static uint32_t Jitter = 0;

static uint32_t CalibratedFreq = 0;
static float C = 0.0;

typedef enum {
	InductanceMode,
	CapacitanceMode
} mode_t;

static mode_t Mode = InductanceMode;

button_t calibrationButton, modeButton;

/*************************************************************************/

/*
 * Timer1 is used in normal mode to count falling edges on T1 (PD5), 
 * which corresponds to the current oscillator frequency.
 *
 * Timer0 is used in CTC mode to take a reading from and clear Timer1 at 10Hz
 * Timer0 counts at 16MHz/256 = 62.5 kHz
 * Using a TOP of 249 will trigger an interrupt at 250 Hz
 * A soft timer will count interrupts and trigger an update when it reaches 25
 */

static void setupTimers() {
	// Setup Timer0
	OCR0A = 249;				// Set CTC TOP
	TCCR0A |= (1 << WGM01);		// Enable CTC
	TCCR0B |= (1 << CS02);		// Use prescaler of Fclk/1024
	TIMSK0 |= (1 << OCIE0A);	// Enable interrupts for OCF0A

	// Setup Timer1
	TCCR1B |= (1 << CS11) | (1 << CS12);	// Select falling edge on T1 as clock source
}

/*************************************************************************/
static uint8_t T0SoftCount = 0;

ISR(TIMER0_COMPA_vect) {
	++T0SoftCount;

	// To store the current oscillator reading every 1/10th of a second,
	// with Timer0 running at 250 Hz, update on every 25th interrupt
	if (T0SoftCount == 25) {
		cli();
		
		// Store the current frequency reading in place of the oldest one
		FreqReadingIndex = (1 + FreqReadingIndex) % NUM_READINGS;
		FreqReadings[FreqReadingIndex] = TCNT1;
		
		// Reset Timer0 and the soft count
		TCNT1 = 0;
		T0SoftCount = 0;
		
		// Determine the current oscillator frequency by summing all ten partial readings.
		// Each value in FreqReadings is the number of cycles in 1/10th of a second.
		CurrentFreq = 0;
		for (int index = 0; index < NUM_READINGS; ++index)
			CurrentFreq += FreqReadings[index];
		
		// Determine how much jitter there is between consecutive readings.
		// The frequency difference between readings 0 and 9 is ignored since those readings are not consecutive.
		Jitter = 0;
		for (int index = 1; index < NUM_READINGS; ++index) {
			int32_t a = FreqReadings[index], b = FreqReadings[index - 1];
			Jitter += abs(a - b);
		}
		
		sei();
	}
}

/*************************************************************************/
static void setupIO(void) {
	// Enable pull-ups for button input pins
	PORTB = (1 << PORTB1) | (1 << PORTB2);
	
	// Configure outputs
	DDRC = 0b00111111;
	DDRD = 0b11000011;
	
	// Initialise buttons
	initButton(&calibrationButton);
	initButton(&modeButton);
}

/*************************************************************************/
static char *format(const char *name, float x, char *unit) {
	char *prefixes[] = { "G", "M", "k", "", "m", "u", "n", "p" };
	int index = 3, indexMax = 7;
	
	x -= fmodf(x, 1e-14f);
	
	// Re-scale the value until it is in the range 0..<1000, and select an appropriate unit multiplier
	while (x < 1.0f && index < indexMax) {
		x *= 1000.0f;
		index += 1;
	}

	while (x > 1000.0f && index > 0) {
		x /= 1000.0f;
		index -= 1;
	}
	
	char *result = malloc(16 * sizeof(char));
	snprintf(result, 16, "%s = %-4.4g %s%s", name, x, prefixes[index], unit);
	
	return result;
}

/*************************************************************************/
static void setMode(mode_t newMode) {
	Mode = newMode;
	setBit(PORTC, PORTC1, Mode == CapacitanceMode);
}

/*************************************************************************/
static void calibrate(void) {
	lcd_clrscr();
	lcd_puts("Calibrating...");
	
	// Change to capacitance measurement mode
	mode_t oldMode = Mode;
	setMode(CapacitanceMode);
	
	// Take a stable frequency reading with just the LC tank in circuit
	_delay_ms(500);
	while (Jitter > 5)
		_delay_ms(100);
	
	CalibratedFreq = CurrentFreq;
	
	// Switch in the calibration capacitor and take another frequency reading
	setBit(PORTC, PORTC0, 1);
	
	_delay_ms(500);
	while (Jitter > 5)
		_delay_ms(100);
	
	uint32_t f2 = CurrentFreq;
	
	float fr = (float)CalibratedFreq / (float)f2;
	C = (1000.0f / 1e12f) / (fr*fr - 1);
	
	// Switch out the calibration cap and revert to the previous measurement mode
	setBit(PORTC, PORTC0, 0);
	setMode(oldMode);
	
	// Display results
	lcd_goto(0x40);
	char *calStr = format("C1", C, "F");
	lcd_puts(calStr);
	free(calStr);
	
	_delay_ms(3000);
}

/*************************************************************************/
static void updateDisplays() {
	// Grab a copy of the current freq and jitter to use for the current display cycle
	cli();
	uint32_t currentFreq = CurrentFreq;
	uint32_t currentJitter = Jitter;
	sei();
		
	// Update the "Stopped" and "Unstable" LEDs
	setBit(PORTD, PORTD6, currentFreq < 20000);
	setBit(PORTD, PORTD7, currentJitter > 5);
	
	// Update the LCD
	lcd_clrscr();
	
	if (Mode == InductanceMode && currentFreq == 0) {
		// If we're in inductance mode and nothing is connected, display a message
		lcd_puts("   Connect an   ");
		lcd_goto(0x40);
		lcd_puts("    Inductor    ");
		return;
	}
	
	char *valueStr = NULL;
	if (Mode == CapacitanceMode) {
		float fr = (float)CalibratedFreq / (float)currentFreq;
		float cx = C * (fr*fr - 1);
		valueStr = format("C", cx, "F");
	} else {
		float f1s = (float)CalibratedFreq * (float)CalibratedFreq;
		float f2s = (float)currentFreq * (float)currentFreq;
		float lx = (1.0f / (C * TWO_PI_SQR)) * (1.0f/f2s - 1.0f/f1s);
		valueStr = format("L", lx, "H");
	}
	
	lcd_puts(valueStr);
	free(valueStr);
	
	char *infoStr = NULL;
	if (currentJitter > 5)
		infoStr = format("J", currentJitter, "Hz/s");
	else
		infoStr = format("F", currentFreq, "Hz");
	
	lcd_goto(0x40);
	lcd_puts(infoStr);
	free(infoStr);
}

/*************************************************************************/
int main() {
	for (int index = 0; index < NUM_READINGS; ++index)
		FreqReadings[index] = 0;
	
	setupIO();
	setupTimers();
	lcd_init();
	
	sei();
	
	setMode(CapacitanceMode);
	calibrate();
	
	for(uint32_t count = 0; 1; ++count) {
		if (updateButton(&calibrationButton, PINB & (1 << PINB2)) && (calibrationButton.state == 0))
			calibrate();
		
		if (updateButton(&modeButton, PINB & (1 << PINB1)) && (modeButton.state == 0))
			setMode((Mode == CapacitanceMode) ? InductanceMode : CapacitanceMode);
		
		if (count % 8)
			updateDisplays();
		
		_delay_ms(50);
	}

	return 0;
}
