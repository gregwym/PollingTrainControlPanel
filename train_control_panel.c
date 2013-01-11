#include <plio.h>
#include <bwio.h>
#include <ts7200.h>
#include <debug.h>

#define FALSE 0x00000000
#define TRUE 0xffffffff

/* Timer Constants */
#define TIMER_MIN 0x00000000
#define TIMER_MAX 0xffffffff

/* ASCI Constants */
#define ASCI_ESC 27
#define ASCI_CLEAR_SCREEN "2J"
#define ASCI_CLEAR_TO_EOL "K"
#define ASCI_CURSOR_SAVE "s"
#define ASCI_CURSOR_RETURN "u"
#define ASCI_CURSOR_TO "H"
#define ASCI_BACKSPACE '\b'

/* Screen formatting */
#define NO_ARG 0xffffffff

#define LINE_ELAPSED_TIME 1
#define LINE_LAST_COMMAND 2
#define LINE_USER_INPUT 20
#define LINE_DEBUG 25
#define LINE_BOTTOM 35

#define COLUMN_FIRST 1
#define COLUMN_SENSOR_WIDTH 4

/* User Inputs */
#define USER_INPUT_MAX 1000
#define USER_COMMAND_QUIT 1

/* Train Control */
#define SYSTEM_START 96
#define SYSTEM_STOP 97

#define TRAIN_REVERSE 15

#define SWITCH_STR 33
#define SWITCH_CUR 34
#define SWITCH_OFF 32

#define SENSOR_READ_ONE 192
#define SENSOR_READ_MULTI 128
#define SENSOR_TOTAL 5
#define SENSOR_BYTE_EACH 2
#define SENSOR_RECENT 5
#define SENSOR_BIT_MASK 0x01

/* Global Variable Declarations */
unsigned int dbflags = 0;

unsigned int previous_timer_value = 0;
unsigned int timer_tick_remained = 0;
unsigned int tenth_sec_elapsed = 0;

char user_input_buffer[USER_INPUT_MAX] = {'\0'};
unsigned int user_input_size = 0;

char sensor_data[SENSOR_TOTAL * SENSOR_BYTE_EACH] = {};
char sensor_ids[SENSOR_TOTAL] = {};
unsigned int sensor_data_next = 0;
unsigned int recent_sensor[SENSOR_RECENT] = {};
unsigned int recent_sensor_index = 0;

/*
 * Hardware Register Manipulation
 */

int getRegister(int base, int offset) {
	int *addr = (int *)(base + offset);
	return *addr;
}

int getRegisterBit(int base, int offset, int mask) {
	return (getRegister(base, offset)) & mask;
}

void setRegister(int base, int offset, int value) {
	int *addr = (int *)(base + offset);
	*addr = value;
}

void setRegisterBit(int base, int offset, int mask, int value) {
	int buf = getRegister(base, offset);
	buf = value ? buf | mask : buf & ~mask;
	setRegister(base, offset, buf);
}

/* 
 * IO Control
 */

void printAsciControl(int channel, char *control, int arg1, int arg2) {
	plprintf(channel, "%c[", ASCI_ESC);
	if(arg1 != NO_ARG) plprintf(channel, "%d", arg1);
	if(arg2 != NO_ARG) plprintf(channel, ";%d", arg2);
	plprintf(channel, "%s", control);
}

/* 
 * Timer Control
 */

unsigned int setTimerControl(int timer_base, unsigned int enable, unsigned int mode, unsigned int clksel) {
	unsigned int* timer_control_addr = (unsigned int*) (timer_base + CRTL_OFFSET);
	DEBUG(DB_TIMER, "Timer3 base: 0x%x ctrl addr: 0x%x offset: 0x%x.\n", timer_base, timer_control_addr, CRTL_OFFSET);

	unsigned int control_value = (ENABLE_MASK & enable) | (MODE_MASK & mode) | (CLKSEL_MASK & clksel) ;
	DEBUG(DB_TIMER, "Timer3 control changing from 0x%x to 0x%x.\n", *timer_control_addr, control_value);

	*timer_control_addr = control_value;
	return *timer_control_addr;
}

unsigned int getTimerValue(int timer_base) {
	unsigned int* timer_value_addr = (unsigned int*) (timer_base + VAL_OFFSET);
	unsigned int value = *timer_value_addr;
	return value;
}

void handleTimeElapse() {
	unsigned int timer_value = getTimerValue(TIMER3_BASE);
	unsigned int time_elapsed = previous_timer_value - timer_value;
	
	// Fix time_elapsed when underflow
	if(timer_value > previous_timer_value) {
		time_elapsed = previous_timer_value + (TIMER_MAX - timer_value) + 1;
	}
	
	// If time elapsed more than 1/10 sec
	if(time_elapsed >= 200)
	{
		// Add elapsed time into remaining ticks, then convert to tenth-sec
		timer_tick_remained += time_elapsed;
		for(;timer_tick_remained >= 200; timer_tick_remained -= 200) tenth_sec_elapsed++;
		previous_timer_value = timer_value;
	
		printAsciControl(COM2, ASCI_CURSOR_TO, LINE_ELAPSED_TIME, COLUMN_FIRST);
		plprintf(COM2, "Time elapsed: %d:%d,%d, timer value: 0x%x\n", tenth_sec_elapsed / 600, (tenth_sec_elapsed % 600) / 10, tenth_sec_elapsed % 10, timer_value);
		printAsciControl(COM2, ASCI_CURSOR_TO, LINE_USER_INPUT, user_input_size + 1);
	}
}

/*
 * User Interactions
 */

// export the first token to 'token' and return the address of the start of next token (maybe EOL)
const char *str2token(const char *str, char *token) {
	while(*str != '\0' && *str != ' ' && *str != '\n' && *str != '\r') *token++ = *str++;
	*token = '\0';
	while(*str == ' ') str++;
	return str;
}

int strcmp(const char *src, const char *dst) {
	int ret = 0;
	while( ! (ret = *(unsigned char *)src - *(unsigned char *)dst) && *dst) ++src, ++dst;
	if ( ret < 0 ) ret = -1 ;
	else if ( ret > 0 ) ret = 1 ;
	return( ret );
}

int atoi(const char *str, int base) {
	int sign = 1, n = 0;
	while(*str == ' ') str++;
	sign = 1;
	switch(*str) {
		case '-': sign = -1;
		default: 
			break;
	}
	while((*str) >= '0' && (*str) <= '9') n = base * n + (*str++ - '0');
	return (sign * n);
}

// Return: -1 Invalid command; 1 System command; 0 Normal command
int handleUserCommand() {
	// Single char command
	if(user_input_size == 2) {
		switch(user_input_buffer[0]) {
			case 'g':
				DEBUG(DB_TRAIN_CTRL, "Sending start\n");
				plputc(COM1, SYSTEM_START);
				break;
			case 's':
				DEBUG(DB_TRAIN_CTRL, "Sending stop\n");
				plputc(COM1, SYSTEM_STOP);
				break;
			default:
				break;
		}
		return 1;
	}
	
	const char *str = user_input_buffer;
	char command[10], token[10];
	command[0] = '\0';
	token[0] = '\0';
	str = str2token(str, command);
	printAsciControl(COM2, ASCI_CURSOR_TO, LINE_DEBUG, COLUMN_FIRST);
	DEBUG(DB_USER_INPUT, "User Input: Extracted command %s from 0x%x to 0x%x\n", command, user_input_buffer, str);
	
	if(strcmp(command, "tr") == 0 || strcmp(command, "rv") == 0 || strcmp(command, "sw") == 0) {
		str = str2token(str, token);
		unsigned char number = atoi(token, 10);
		
		printAsciControl(COM2, ASCI_CURSOR_TO, LINE_DEBUG + 1, COLUMN_FIRST);
		DEBUG(DB_USER_INPUT, "User Input: Arg1 0x%x\n", number);
		str = str2token(str, token);
		unsigned char value = 0;
		switch(command[0]) {
			case 'r':
			case 't':
				value = (command[0] == 'r') ? TRAIN_REVERSE : atoi(token, 10);
				printAsciControl(COM2, ASCI_CURSOR_TO, LINE_DEBUG + 2, COLUMN_FIRST);
				DEBUG(DB_USER_INPUT, "User Input: Changing train #%u speed to %u\n", number, value);
				break;
			case 's':
				value = (token[0] == 'S') ? SWITCH_STR : SWITCH_CUR;
				printAsciControl(COM2, ASCI_CURSOR_TO, LINE_DEBUG + 2, COLUMN_FIRST);
				DEBUG(DB_USER_INPUT, "User Input: Assigning switch #%d direction to %s\n", number, token);
				break;
		}
		plputc(COM1, value);
		plputc(COM1, number);
		plputc(COM1, SWITCH_OFF); // Turn off the solenoid
		
		return 0;
	}
	
	return -1;
}

int handleUserInput() {
	char user_input_char = '\0';
	if(plgetc(COM2, &user_input_char) > 0 && user_input_size < USER_INPUT_MAX) {
		
		// Push or pop char from user_input_buffer
		if(user_input_char != ASCI_BACKSPACE) {
			user_input_buffer[user_input_size] = user_input_char;
			user_input_size++;
			user_input_buffer[user_input_size] = '\0';
			printAsciControl(COM2, ASCI_CURSOR_TO, LINE_USER_INPUT, user_input_size);
			plputc(COM2, user_input_char);
		}
		else if(user_input_size > 0){
			user_input_size--;
			user_input_buffer[user_input_size] = '\0';
			printAsciControl(COM2, ASCI_CURSOR_TO, LINE_USER_INPUT, user_input_size + 1);
			printAsciControl(COM2, ASCI_CLEAR_TO_EOL, NO_ARG, NO_ARG);
		}
		
		// If is EOL or buffer full
		if(user_input_char == '\n' || user_input_char == '\r' || user_input_size == USER_INPUT_MAX) {
			// Clear the input line
			printAsciControl(COM2, ASCI_CURSOR_TO, LINE_USER_INPUT, COLUMN_FIRST);
			printAsciControl(COM2, ASCI_CLEAR_TO_EOL, NO_ARG, NO_ARG);
			
			printAsciControl(COM2, ASCI_CURSOR_TO, LINE_DEBUG, COLUMN_FIRST);
			DEBUG(DB_USER_INPUT, "User Input: Reach EOL. Input Size %u, value %s\n", user_input_size, user_input_buffer);
			// If is q, quit
			if(user_input_size == 2 && user_input_buffer[0] == 'q') {
				return USER_COMMAND_QUIT;
			}
			
			handleUserCommand();
			
			// Send to last command
			printAsciControl(COM2, ASCI_CURSOR_TO, LINE_LAST_COMMAND, COLUMN_FIRST);
			printAsciControl(COM2, ASCI_CLEAR_TO_EOL, NO_ARG, NO_ARG);
			plputstr(COM2, user_input_buffer);
			
			// Reset input buffer
			user_input_buffer[0] = '\0';
			user_input_size = 0;
		}
	}
	return 0;
}

/*
 * Sensor Data Collection
 */
void sensorBootstrap(){
	int i;
	for(i = 0; i < SENSOR_TOTAL; i++) {
		sensor_ids[i] = 'A' + i;
	}
	
	// bwprintf(COM2, "%c[%d;%d%s", ASCI_ESC, LINE_DEBUG, COLUMN_FIRST, ASCI_CURSOR_TO);
	// bwprintf(COM2, "Sensor: Booting\n");
	while((!getRegisterBit(UART1_BASE, UART_FLAG_OFFSET, CTS_MASK)) || 
	      (!getRegisterBit(UART1_BASE, UART_FLAG_OFFSET, TXFE_MASK)) || 
	      (!getRegisterBit(UART1_BASE, UART_FLAG_OFFSET, RXFE_MASK))) {
		// bwprintf(COM2, ".");
		if((!getRegisterBit(UART1_BASE, UART_FLAG_OFFSET, RXFE_MASK))) {
			// char c = 
			bwgetc(COM1);
			// bwprintf(COM2, "Sensor: Consuming sensor data 0x%x\n", c);
		}
	}
	plputc(COM1, SENSOR_READ_MULTI + SENSOR_TOTAL);
	// bwprintf(COM2, "Sensor: Sent read requrest\n");
}

void saveSensorData(unsigned int index, char new_data) {
	// Save to sensor_data
	char old_data = sensor_data[index];
	sensor_data[index] = new_data;
	
	if(old_data != new_data) {
		int i;
		char old_bit, new_bit;
		for(i = 0; i < 8; i++) {
			old_bit = (old_data >> i) & SENSOR_BIT_MASK;
			new_bit = (new_data >> i) & SENSOR_BIT_MASK;
			if(old_bit != new_bit) {
				int bit_id = (8 * (index % 2)) + (8 - i);
				printAsciControl(COM2, ASCI_CURSOR_TO, LINE_DEBUG - 1, COLUMN_FIRST);
				DEBUG(DB_SENSOR, "#%c%d %x -> %x\n", sensor_ids[index / 2], bit_id, old_bit, new_bit);
				
				// printAsciControl(COM2, ASCI_CURSOR_TO, LINE_RECENT_SENSOR, COLUMN_FIRST + (recent_sensor_index * COLUMN_SENSOR_WIDTH));
				// plprintf(COM2, "|%d%d:%x", index, i, new_bit);
				// recent_sensor_index = (recent_sensor_index + 1) % SENSOR_RECENT;
			}
		}
	}
	
	printAsciControl(COM2, ASCI_CURSOR_TO, LINE_DEBUG + index, COLUMN_FIRST);
	DEBUG(DB_SENSOR, "%c: 0x%x\n", sensor_ids[index / 2], new_data);
}
void collectSensorData() {
	char sensor_new_data = '\0';
	if(plgetc(COM1, &sensor_new_data) > 0) {
		saveSensorData(sensor_data_next, sensor_new_data);
		
		// If have load last sensor data in a row, request for sensor data again.
		if(sensor_data_next == (SENSOR_TOTAL * SENSOR_BYTE_EACH) - 1) {
			plputc(COM1, SENSOR_READ_MULTI + SENSOR_TOTAL);
			DEBUG(DB_SENSOR, "Sent %d\n", sensor_data_next);
		}
		
		sensor_data_next = (sensor_data_next + 1) % (SENSOR_TOTAL * SENSOR_BYTE_EACH);
		printAsciControl(COM2, ASCI_CURSOR_TO, LINE_DEBUG + SENSOR_TOTAL * SENSOR_BYTE_EACH, COLUMN_FIRST);
		DEBUG(DB_SENSOR, "Next %d\n", sensor_data_next);
	}
}

/* 
 * Main Polling Loop
 */
void pollingLoop() {
	/* Initialize Elapsed time tracker */
	previous_timer_value = getTimerValue(TIMER3_BASE);
	timer_tick_remained = 0;
	tenth_sec_elapsed = 0;
	
	/* Initialize User Input Buffer */
	user_input_size = 0;
	user_input_buffer[user_input_size] = '\0';
	
	sensor_data_next = 0;
	recent_sensor_index = 0;
	
	/* Initialize Sensor Data Request */
	sensorBootstrap();
	
	/* Polling loop */
	while(TRUE) {
		/* Sensor: Collect and display data */
		collectSensorData();
		
		/* Polling IO: Give it a chance to send out char */
		plsend(COM1);
		plsend(COM2);
		
		/* Timer: Calculate and display time elapsed */
		handleTimeElapse();
		
		/* User Input */
		if(handleUserInput() == USER_COMMAND_QUIT) break;
	}
}

int main(int argc, char* argv[]) {
	
	/* Initialize Global Variables */
	char plio_buffer[CHANNEL_COUNT * OUTPUT_BUFFER_SIZE];
	unsigned int plio_send_index[CHANNEL_COUNT];
	unsigned int plio_save_index[CHANNEL_COUNT];
	dbflags = /*DB_IO | DB_TIMER | DB_USER_INPUT | DB_TRAIN_CTRL |*/ DB_SENSOR; // Debug Flags
	
	/* Initialize IO: setup buffer; BOTH: turn off fifo; COM1: speed to 2400, enable stp2 */
	plbootstrap(plio_buffer, plio_send_index, plio_save_index);
	plsetfifo(COM2, OFF);
	plsetfifo(COM1, OFF);
	plsetspeed(COM1, 2400);
	setRegisterBit(UART1_BASE, UART_LCRH_OFFSET, STP2_MASK, TRUE);
	
	printAsciControl(COM2, ASCI_CLEAR_SCREEN, NO_ARG, NO_ARG);
	
	/* Verifiying COM1's Configuration: nothing when debug flag is turned off */
	printAsciControl(COM2, ASCI_CURSOR_TO, LINE_DEBUG, COLUMN_FIRST);
	DEBUG(DB_IO, "COM1 LCRH: 0x%x\n", getRegister(UART1_BASE, UART_LCRH_OFFSET)); // 0x68
	DEBUG(DB_IO, "COM1 LCRM: 0x%x\n", getRegister(UART1_BASE, UART_LCRM_OFFSET)); // 0x0
	DEBUG(DB_IO, "COM1 LCRL: 0x%x\n", getRegister(UART1_BASE, UART_LCRL_OFFSET)); // 0xbf
	DEBUG(DB_IO, "COM1 CTRL: 0x%x\n", getRegister(UART1_BASE, UART_CTLR_OFFSET)); // 0x1
	DEBUG(DB_IO, "COM1 FLAG: 0x%x\n", getRegister(UART1_BASE, UART_FLAG_OFFSET)); // 0x91
	DEBUG(DB_IO, "IO Initialized.\n");
	
	/* Initialize Timer: Enable Timer3 with free running mode and 2kHz clock */
	setTimerControl(TIMER3_BASE, TRUE, FALSE, FALSE);
	DEBUG(DB_TIMER, "Timer3 value start with 0x%x.\n", getTimerValue(TIMER3_BASE));
	
	pollingLoop();
	
	setTimerControl(TIMER3_BASE, FALSE, FALSE, FALSE);
	printAsciControl(COM2, ASCI_CURSOR_TO, LINE_BOTTOM, COLUMN_FIRST);
	
	plflush(COM1);
	plflush(COM2);
	
	plstat();
	return 0;
}
