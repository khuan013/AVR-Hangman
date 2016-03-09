// Hangman Project main.c
// Kenneth Huang khuan013

#include <avr/io.h>
#include "keypad.h"
#include "io.c"
#include <avr/interrupt.h>
#include <string.h>
#include <avr/sleep.h>
#include <stdlib.h>


// LEDMATRIX shift register functions
// portB 0-4
void transmit_col(unsigned char data) {
	int i;
	for (i = 7; i >= 0; --i) {
		PORTB = 0x08;
		PORTB |= ((data >> i) & 0x01);
		PORTB |= 0x04;
	}
	PORTB |= 0x02;
	PORTB = 0x00;
}
// portA 2-6
void transmit_row(unsigned char data) {
	int i;
	unsigned char prev_A = (PORTA & 0x03);
	for (i = 7; i >= 0; --i) {
		PORTA = 0x20 + prev_A;
		PORTA |= ((data >> i) & 0x01) << 2;
		PORTA |= 0x10;
	}
	PORTA |= 0x08 + prev_A;
}


//PWM on B6
void set_PWM(double frequency) {

	static double current_frequency;
	if (frequency != current_frequency) {
		if (!frequency) TCCR3B &= 0x08; //stops timer/counter
		else TCCR3B |= 0x03; // resumes/continues timer/counter
		if (frequency < 0.954) OCR3A = 0xFFFF;
		else if (frequency > 31250) OCR3A = 0x0000;
		else OCR3A = (short)(8000000 / (128 * frequency)) - 1;
		TCNT3 = 0; // resets counter
		current_frequency = frequency;
	}
}
void PWM_on() {
	TCCR3A = (1 << COM3A0);
	TCCR3B = (1 << WGM32) | (1 << CS31) | (1 << CS30);
	set_PWM(0);
}
void PWM_off() {
	TCCR3A = 0x00;
	TCCR3B = 0x00;
}

// task definition
typedef struct task {
	int state;                  // Task's current state
	unsigned long period;       // Task period
	unsigned long elapsedTime;  // Time elapsed since last task tick
	int (*TickFct)(int);        // Task tick function
} task;

// 4 concurrent SM tasks
task tasks[4];
const unsigned char tasksNum = 4;
const unsigned long tasksPeriodGCD = 5;

// timer interrupt service routine
void TimerISR() {
	unsigned char i;
	for (i = 0; i < tasksNum; ++i) { // Heart of the scheduler code
		if ( tasks[i].elapsedTime >= tasks[i].period ) { // Ready
			tasks[i].state = tasks[i].TickFct(tasks[i].state);
			tasks[i].elapsedTime = 0;
		}
		tasks[i].elapsedTime += tasksPeriodGCD;
	}
}

// TimerISR() sets this to 1. C programmer should clear to 0.
volatile unsigned char TimerFlag = 0;
// Internal variables for mapping AVR's ISR to our cleaner TimerISR model.
unsigned long _avr_timer_M = 1; // Start count from here, down to 0. Default 1 ms.
unsigned long _avr_timer_cntcurr = 0; // Current internal count of 1ms ticks
void TimerOn() {
	TCCR1B = 0x0B;
	OCR1A = 125;
	TIMSK1 = 0x02;
	TCNT1=0;
	_avr_timer_cntcurr = _avr_timer_M;
	SREG |= 0x80;
}
void TimerOff() {
	TCCR1B = 0x00;
}
ISR(TIMER1_COMPA_vect) {
	_avr_timer_cntcurr--;
	if (_avr_timer_cntcurr == 0) {
		TimerISR();
		_avr_timer_cntcurr = _avr_timer_M;
	}
}
void TimerSet(unsigned long M) {
	_avr_timer_M = M;
	_avr_timer_cntcurr = _avr_timer_M;
}

// global variables
unsigned char key;
int startpause = 0;
int alph_index = 0;
int a_press = 0;
int title_song = 0;

// Keypad function
enum KP_States { KP_wait,  wait_release};
int KP_Tick(int state) {
		
	switch (state) {
		case KP_wait:
			if (GetKeypadKey()) {
				key = GetKeypadKey();
				if (key == '*')
					startpause = 1;
				
				else if (key == '1') {
					alph_index++;
					if (alph_index >= 26)
						alph_index = 0;
				}
				else if (key == '2') {
					alph_index--;
					if (alph_index < 0)
						alph_index = 25;
				}
				else if (key == 'A') {
					a_press = 1;
				}
				else if (key == 'D') {
					title_song = 0;
				}
				
				state = wait_release;
			}
			else
				state = KP_wait;
			break;
		
		//debounce
		case wait_release:
			if (GetKeypadKey())
				state = wait_release;
			else
				state = KP_wait;
			break;
			
	}

	return state;
}
	

// Words used for hangman
const char *a[] = {"hello", "world", "money", "tiger"};
const int WORD_BANK = 4; // word bank size
int sp_word = 0; //chooses the word to play
enum DP_States { DP_wait, wait_game, START, PLAY, LOSE, WIN, WAITPRESS};

// alphabet
const char alphabet[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i',
						'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',
						't', 'u', 'v', 'w', 'x', 'y', 'z'};

//variables to determine which sound to play
int speaker_on = 0;
int goodorbad = 0;
int win_game = 0;
int lose_game = 0;
int pressed_sound=0;

// keep track of correct/incorrect guesses
int num_wrong = 0;
int num_right = 0;

int string_length = 0;
int r = 0;

//led matrix
int gamestart = 0;

// Main Game tick function and LCD display
int DP_Tick(int state) {

	switch(state) {
		case DP_wait:
			// reset variables
			num_right = 0;
			num_wrong = 0;
			alph_index = 0;			
			
			// play title
			title_song = 1;
			speaker_on = 1;			

			//ledmat
			gamestart = 0;
			
			//initial display
			LCD_DisplayString(1, "Hangman- Press *");	
			state = wait_game;		
			break;
		case wait_game:
			if (startpause == 1) {
				LCD_ClearScreen();
				state = START;
				startpause = 0;
			}
			else
				state = wait_game;
			break;
			
		case START:
			
			// turn off title_song, setup led matrix
			title_song = 0;
			gamestart = 1;
		
			// randomize the word in play
			r = random() % WORD_BANK;
			string_length = strlen(a[r]);
			
			//display '-' for each character in the word			
			for (int i = 1; i <= string_length; i++) {
				LCD_Cursor(i);
				LCD_WriteData('-');
			}
			
			state = PLAY;
			break;
		
		case PLAY:
			// display current selected character
			LCD_Cursor(32);
			LCD_WriteData(alphabet[alph_index]);
		
			//pause
			if (startpause == 1) {
				LCD_ClearScreen();
				state = DP_wait;
				startpause = 0;
				break;
			}
			// if a character is entered, check if it exists in word
			else if (a_press == 1) {
				a_press = 0;
				pressed_sound = 1;
				
				int no_matches = 0; // keeps track of number of matches
				// iterate through word to check if character exists
				for (int i = 0; i < string_length; i++) {
					if (a[r][i] == alphabet[alph_index]) {
						//num_right++;
						LCD_Cursor(i+1);
						LCD_WriteData(alphabet[alph_index]);
						speaker_on = 1;
						goodorbad = 1;
					}
					else {
						no_matches++;
					}
						
				}
				if (no_matches == string_length) {
					LCD_Cursor(17 + num_wrong);
					num_wrong++;
					LCD_WriteData(alphabet[alph_index]);
					speaker_on = 1;
					goodorbad = 0;		
				}
				else
					num_right++;
			}
			//win condition
			else if (num_right == string_length) {
				state = WIN;
				win_game = 1;
			}
			//lose condition
			else if (num_wrong >= 10) { 
				state = LOSE;
				lose_game = 1;
			}
				
			break;
				
			
		case LOSE:
				LCD_ClearScreen();
				LCD_DisplayString(17, "YOU LOSE!!!");
				for (int i=0; i < string_length; i++) {
					LCD_Cursor(i+1);
					LCD_WriteData(a[r][i]);
				}
				LCD_Cursor(32);
				state = WAITPRESS;
				break;
			
		case WIN:
				LCD_ClearScreen();
				LCD_DisplayString(17, "YOU WIN!!!");
				for (int i=0; i < string_length; i++) {
					LCD_Cursor(i+1);
					LCD_WriteData(a[r][i]);
				}
				LCD_Cursor(32);
				state = WAITPRESS;
				break;
				
		case WAITPRESS:
			if (startpause == 1) {
				LCD_ClearScreen();
				state = DP_wait;
				startpause = 0;	
			}
			else
				state = WAITPRESS;
			
				break;
			
			break;
	}
	
	return state;
}

// Frequency arrays for sound using PWM
double bad[] = {523.25, 493.88};
double good[] = {523.25, 659.25};
int duration[] = {30, 20};
	
double win_sound[] = {0, 659.25, 698.46, 739.99, 783.99, };
int win_numbers[] = {100, 50, 50, 50, 80}; 
	
double lose_sound[] = {0, 523.25, 493.88, 466.16, 440.00, 415.30, 392.00};
int lose_numbers[] = {100, 35, 35, 35, 35, 70, 40};

//title song	
double fairy_fount[] = {1760.00, 1174.66, 987.77, 783.99, 1567.98, 1174.66, 987.77, 783.99, 1470.98, 1174.66, 987.77, 783.99, 1567.98, 1174.66, 987.77, 783.99,
						1567.98, 1046.50, 880.00, 698.46, 1396.91, 1046.50, 880.00, 698.46, 1318.51, 1046.50, 880.00, 698.46, 1396.91, 1046.50, 880.00, 698.46,
						1396.91, 932.33, 783.99, 659.25, 1318.51, 932.33, 783.99, 659.25, 1244.51, 932.33, 783.99, 659.25, 1318.51, 932.33, 783.99, 659.25,
						1318.51, 880.00, 698.46, 587.33, 1174.66, 880.00, 698.46, 587.33, 1108.73, 880.00, 698.46, 587.33, 1174.66, 880.00, 698.46, 587.33,
						1760.00, 1174.66, 987.77, 783.99, 1567.98, 1174.66, 987.77, 783.99, 1470.98, 1174.66, 987.77, 783.99, 1567.98, 1174.66, 987.77, 783.99,
						1864.66, 1244.51, 1046.50, 739.99, 1760.00, 1244.51, 1046.50, 739.99, 1661.22, 1244.51, 1046.50, 739.99, 1760.00, 1244.51, 1046.50, 739.99,
						2093.00, 1174.66, 932.33, 783.99, 1864.66, 1174.66, 932.33, 783.99, 1760, 1174.66, 932.33, 783.99, 1864.66, 1174.66, 932.33, 783.99,
						1760.00, 932.33, 783.99, 659.25, 1567.98, 932.33, 783.99, 659.25, 1396.91, 932.33, 783.99, 659.25, 1318.51, 932.33, 783.99, 659.25};
							
int fairy_number = 25;
	
// SM for speaker output
enum SP_States{ SP_wait, output} SP_state;
unsigned int count = 0;
unsigned int ind = 0;

int SP_Tick(state) {
	switch(state) {
		case SP_wait:
			count = 0;
			ind = 0;
			set_PWM(0);
			if (speaker_on == 1) {
				state = output;
				speaker_on = 0;
			}
			else {
				state = SP_wait;
			}

		
		break;
		
		case output:
			ind++;
			
			if (title_song) {
				
				set_PWM(fairy_fount[count]);
				if (ind >= fairy_number) {
					count++;
					ind = 0;
				}
				if (count >= 128) {
					count = 0;
				}
				
			}
			
			else if (win_game) {
				set_PWM(win_sound[count]);
				if (ind >= win_numbers[count]) {
					count++;
					ind = 0;
				}
				
				if (count >= 5) {
					state = SP_wait;
					win_game = 0;
					break;
				}
			}
			
			else if (lose_game) {
				set_PWM(lose_sound[count]);
				if (ind >= lose_numbers[count]) {
					count++;
					ind = 0;
				}
				
				if (count >= 7) {
					state = SP_wait;
					lose_game = 0;
					break;
				}
			}
			else if (pressed_sound) {
				if (ind >= duration[count]) {
					count++;
					ind = 0;
				}
				
				if (count >= 2) {
					pressed_sound = 0;
					state = SP_wait;
					break;
				}
				
				if (goodorbad == 0)
				set_PWM(bad[count]);
				else
				set_PWM(good[count]);
				state = output;
			}
			else
				state = SP_wait;
		
		break;
		
		
		
		default:
			break;
	}
	
	return state;
}


// Enable each row one position at a time.
// column arrays for LED matrix output
unsigned char rows[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80}; 
unsigned char cols1[] = {0x15, 0xBB, 0xA0, 0xBB, 0xB1, 0xB5, 0xB1, 0x83};

// 11 stages of hangman, including blank matrix
unsigned char cols2[11][8] = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
						{0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
						{0x1F, 0xBF, 0xBF, 0xBF, 0xBF, 0xFF, 0xFF, 0xFF},
						{0x1F, 0xBF, 0xBF, 0xBF, 0xBF, 0xBF, 0xBF, 0xBF},
						{0x1F, 0xBF, 0xBF, 0xBF, 0xBF, 0xBF, 0xBF, 0x83},
						{0x1F, 0xBF, 0xBF, 0xBF, 0xB1, 0xB5, 0xB1, 0x83},
						{0x1F, 0xBB, 0xBB, 0xBB, 0xB1, 0xB5, 0xB1, 0x83},
						{0x17, 0xBB, 0xBB, 0xBB, 0xB1, 0xB5, 0xB1, 0x83},
						{0x15, 0xBB, 0xBB, 0xBB, 0xB1, 0xB5, 0xB1, 0x83},
						{0x15, 0xBB, 0xA3, 0xBB, 0xB1, 0xB5, 0xB1, 0x83},
						{0x15, 0xBB, 0xA0, 0xBB, 0xB1, 0xB5, 0xB1, 0x83}};

// win animation						
unsigned char cols3[6][8] =	{{0x15, 0xBB, 0xA0, 0xBB, 0xB1, 0xB5, 0xB1, 0x83},
							{0x1A, 0xBD, 0xB0, 0xBD, 0xB8, 0xBA, 0xB8, 0x83},
							{0x1D, 0xBE, 0xB8, 0xBE, 0xBC, 0xBD, 0xBC, 0x83},
							{0x1E, 0xBF, 0xBC, 0xBF, 0xBE, 0xBE, 0xBE, 0x83},
							{0x1F, 0xBF, 0xBE, 0xBF, 0xBF, 0xBF, 0xBF, 0x83},
							{0x1F, 0xBF, 0xBF, 0xBF, 0xBF, 0xBF, 0xBF, 0x83}};

// LED matrix SM
enum LD_States{ LD_wait, LD_play, LD_win} LD_state;
int ledm = 0;	
int j = 0;
int LD_Tick(state) {
	
	switch(state) {
		case LD_wait:
			ledm = 0;
			j = 0;
		
			if (gamestart) {
				state = LD_play;
				break;}
			for (int i = 0; i < 8; i++) {
				unsigned char temp = 0xFF;
				transmit_row(rows[i]);

				transmit_col(cols1[i]);
				delay_ms(1);
				transmit_row(0x00);
				transmit_col(0xFF);
			}
		
			
			break;

		case LD_play:
			if (!gamestart) {
				state = LD_wait;
				break;	
			}
				
			for (int i = 0; i < 8; i++) {
				unsigned char temp = 0xFF;
				transmit_row(rows[i]);

				transmit_col(cols2[num_wrong][i]);
				delay_ms(1);
				transmit_row(0x00);
				transmit_col(0xFF);
			}
			if (win_game)
				state = LD_win;
			break;
			
		case LD_win:
		
				ledm++;
			
				if (ledm >= 40) {
					ledm = 0;
					j++;
					
				}
					
				if (j >= 6)
					j = 5;
					
				for (int i = 0; i < 8; i++) {
					unsigned char temp = 0xFF;
					transmit_row(rows[i]);

					transmit_col(cols3[j][i]);
					delay_ms(1);
					transmit_row(0x00);
					transmit_col(0xFF);
				}
				if (!gamestart) 
					state = LD_wait;
				
				break;
				
	}

	return state;
}

int main(void)
{
	
	
	// PORTA, PORTB, and PORTD set to output
	DDRA = 0xFF; PORTA = 0x00;//LCD
	DDRB = 0xFF; PORTB = 0x00;
	DDRC = 0xF0; PORTC = 0x0F; // keypad
	DDRD = 0xFF; PORTD = 0x00;
	
	// Set up tasks
	unsigned char i = 0;
	
	tasks[i].state = KP_wait;
	tasks[i].period = 50;
	tasks[i].elapsedTime = tasks[i].period;
	tasks[i].TickFct = &KP_Tick;
	
	i++;
	tasks[i].state = DP_wait;
	tasks[i].period = 200;
	tasks[i].elapsedTime = tasks[i].period;
	tasks[i].TickFct = &DP_Tick;
	
	i++;
	tasks[i].state = SP_wait;
	tasks[i].period = 5;
	tasks[i].elapsedTime = tasks[i].period;
	tasks[i].TickFct = &SP_Tick;
	
	i++;
	tasks[i].state = LD_wait;
	tasks[i].period = 10;
	tasks[i].elapsedTime = tasks[i].period;
	tasks[i].TickFct = &LD_Tick;
	
	// Set timer
	TimerSet(tasksPeriodGCD);
	TimerOn();
	
	// initilize LCD and PWM
	LCD_init();
	PWM_on();
	
	while(1) {
	//loop
	}
}