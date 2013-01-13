#include <plio.h>
#include <bwio.h>
#include <ts7200.h>
#include <debug.h>

#define FALSE 0x00000000
#define TRUE 0xffffffff

/* Timer Constants */
#define TIMER_MIN 0x00000000
#define TIMER_MAX 0xffffffff
#define TIMER_CLOCK_BASE 10
#define TIMER_CLOCK_TICK 20

/* ASCI Constants */
#define ASCI_ESC 27
#define ASCI_CLEAR_SCREEN "2J"
#define ASCI_CLEAR_TO_EOL "K"
#define ASCI_CLEAR_LINE "2K"
#define ASCI_CURSOR_SAVE "s"
#define ASCI_CURSOR_RETURN "u"
#define ASCI_CURSOR_TO "H"
#define ASCI_BACKSPACE '\b'

/* Screen formatting */
#define NO_ARG 0xffffffff

#define LINE_ELAPSED_TIME 1
#define LINE_LAST_COMMAND 2
#define LINE_RECENT_SENSOR 3
#define LINE_USER_INPUT 20
#define LINE_DEBUG 25
#define LINE_BOTTOM 35

#define COLUMN_FIRST 1
#define COLUMN_SENSOR_WIDTH 8
#define COLUMN_SENSOR_DEBUG 60

/* User Inputs */
#define USER_INPUT_MAX 100
#define USER_COMMAND_TOKEN_MAX 10
#define USER_COMMAND_QUIT 1

/* Train Control */
#define SYSTEM_START 96
#define SYSTEM_STOP 97

#define TRAIN_COMMAND_BUFFER_MAX 100
#define TRAIN_COMMAND_PAUSE_TIMEOUT 100
#define TRAIN_COMMAND_DEBUG_LINES 15
#define TRAIN_COMMAND_DELAY 1
#define TRAIN_REVERSE 15
#define TRAIN_REVERSE_DELAY 100

#define SWITCH_STR 33
#define SWITCH_CUR 34
#define SWITCH_OFF 32

#define SENSOR_AUTO_RESET 192
#define SENSOR_READ_ONE 192
#define SENSOR_READ_MULTI 128
#define SENSOR_DECODER_TOTAL 5
#define SENSOR_RECENT_TOTAL 8
#define SENSOR_BYTE_EACH 2
#define SENSOR_BYTE_SIZE 8
#define SENSOR_BIT_MASK 0x01
#define SENSOR_REQUEST_TIMEOUT TRAIN_COMMAND_PAUSE_TIMEOUT

/* Global Variable Declarations */

// Debug
unsigned int dbflags = 0;

// Timer
unsigned int previous_timer_value = 0;
unsigned int timer_value_remained = 0;
unsigned int timer_tick = 0;

// User Input
char user_input_buffer[USER_INPUT_MAX] = {'\0'};
unsigned int user_input_size = 0;

// Train Commands
typedef struct TrainCommand {
	char command;
	int delay;
	int pause;
} TrainCommand;
TrainCommand train_commands_buffer[TRAIN_COMMAND_BUFFER_MAX] = {};
unsigned int train_commands_save_index = 0;
unsigned int train_commands_send_index = 0;
int train_commands_pause_time = 0;

// Sensor Data
char sensor_decoder_data[SENSOR_DECODER_TOTAL * SENSOR_BYTE_EACH] = {};
char sensor_decoder_ids[SENSOR_DECODER_TOTAL] = {};
unsigned int sensor_decoder_next = 0;

unsigned int sensor_recent_next = 0;
unsigned int sensor_request_cts = 0;
int sensor_request_time = 0;

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

unsigned int handleTimeElapse() {
	unsigned int timer_value = getTimerValue(TIMER3_BASE);
	unsigned int time_elapsed = previous_timer_value - timer_value;
	
	// Fix time_elapsed when underflow
	if(timer_value > previous_timer_value) {
		time_elapsed = previous_timer_value + (TIMER_MAX - timer_value) + 1;
	}
	
	// If time elapsed more than 1/100 sec
	if(time_elapsed >= TIMER_CLOCK_TICK)
	{
		// Add elapsed time into remaining ticks, then convert to tenth-sec
		timer_value_remained += time_elapsed;
		unsigned int tick_elapsed = timer_value_remained / TIMER_CLOCK_TICK;
		timer_value_remained %= TIMER_CLOCK_TICK;
		timer_tick += tick_elapsed;
		previous_timer_value = timer_value;
		
		printAsciControl(COM2, ASCI_CURSOR_TO, LINE_ELAPSED_TIME, COLUMN_FIRST);
		plprintf(COM2, "Time elapsed: %d:%d.%d Timer: 0x%x\n", (timer_tick / TIMER_CLOCK_BASE) / 600, ((timer_tick / TIMER_CLOCK_BASE) % 600) / 10, (timer_tick / TIMER_CLOCK_BASE) % 10, timer_value);
		printAsciControl(COM2, ASCI_CURSOR_TO, LINE_USER_INPUT, user_input_size + 1);
		
		return tick_elapsed;
	}
	
	return 0;
}

/*
 * Train Control
 */
int pushTrainCommand(char command, int delay, int pause) {
	unsigned int next_index = (train_commands_save_index + 1) % TRAIN_COMMAND_BUFFER_MAX;
	if(next_index != train_commands_send_index) {
		train_commands_buffer[train_commands_save_index].command = command;
		train_commands_buffer[train_commands_save_index].delay = delay;
		train_commands_buffer[train_commands_save_index].pause = pause;
		
		int sending_delay = train_commands_buffer[train_commands_send_index].delay;
		if(sending_delay > TRAIN_COMMAND_DELAY || command < SENSOR_READ_ONE || command > (SENSOR_READ_ONE + SENSOR_DECODER_TOTAL)) {
			DEBUG_JMP(DB_TRAIN_CTRL, LINE_DEBUG + (train_commands_save_index % TRAIN_COMMAND_DEBUG_LINES) + 1, COLUMN_FIRST, "%d <- %d %d %d", train_commands_save_index, command, delay, pause);
			DEBUG_JMP(DB_TRAIN_CTRL, LINE_DEBUG + (next_index % TRAIN_COMMAND_DEBUG_LINES) + 1, COLUMN_FIRST, "%c[%s", ASCI_ESC, ASCI_CLEAR_LINE);
		}
		
		train_commands_save_index = next_index;
		
		return 1;
	}
	
	DEBUG_JMP(DB_TRAIN_CTRL, LINE_DEBUG - 1, COLUMN_FIRST, "Command buffer full\n");
	return 0;
}

int popTrainCommand(unsigned int tick_elapsed) {
	if(train_commands_pause_time > 0) {
		if(tick_elapsed > 0) {
			train_commands_pause_time -= tick_elapsed;
		}
		else return -1;
		
		if(train_commands_pause_time <= 0) DEBUG_JMP(DB_TRAIN_CTRL, LINE_DEBUG - 1, COLUMN_FIRST, "Override %d\n", train_commands_pause_time);
	}
	
	if(train_commands_send_index != train_commands_save_index) {
		int delay = train_commands_buffer[train_commands_send_index].delay;
		if(delay > 0 && tick_elapsed > 0) {
			delay -= tick_elapsed;
			train_commands_buffer[train_commands_send_index].delay = delay;
			// DEBUG_JMP(DB_TRAIN_CTRL, LINE_DEBUG + (train_commands_send_index % TRAIN_COMMAND_DEBUG_LINES) + 1, COLUMN_FIRST + 20, "-| D:%d", delay);
		}
		
		if(delay <= 0) {
			unsigned int next_index = (train_commands_send_index + 1) % TRAIN_COMMAND_BUFFER_MAX;
			train_commands_pause_time = train_commands_buffer[train_commands_send_index].pause;
			if(train_commands_pause_time > 0) {
				DEBUG_JMP(DB_TRAIN_CTRL, LINE_DEBUG - 1, COLUMN_FIRST, "Paused    \n");
			}
			
			char command = train_commands_buffer[train_commands_send_index].command;
			if(command < SENSOR_READ_ONE || command > (SENSOR_READ_ONE + SENSOR_DECODER_TOTAL)) {
				DEBUG_JMP(DB_TRAIN_CTRL, LINE_DEBUG + (train_commands_send_index % TRAIN_COMMAND_DEBUG_LINES) + 1, COLUMN_FIRST + 20, "-> %d %d %d", command, delay, train_commands_pause_time);
			}
			plputc(COM1, command);
			
			train_commands_send_index = next_index;
			
			return 1;
		}
	}
	
	return 0;
}

/*
 * User Interactions
 */

// export the first token to 'token' and return the address of the start of next token (maybe EOL)
const char *str2token(const char *str, char *token, int token_size) {
	token_size--;
	char *start = token;
	while(*str != '\0' && *str != ' ' && *str != '\n' && *str != '\r' && (token - start) < token_size) *token++ = *str++;
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
				DEBUG(DB_TRAIN_CTRL, "Starting\n");
				pushTrainCommand(SYSTEM_START, TRAIN_COMMAND_DELAY, FALSE);
				break;
			case 's':
				DEBUG(DB_TRAIN_CTRL, "Stoping\n");
				pushTrainCommand(SYSTEM_STOP, TRAIN_COMMAND_DELAY, FALSE);
				break;
			default:
				break;
		}
		return 1;
	}
	
	const char *str = user_input_buffer;
	char command[USER_COMMAND_TOKEN_MAX], token[USER_COMMAND_TOKEN_MAX];
	command[0] = '\0';
	token[0] = '\0';
	str = str2token(str, command, USER_COMMAND_TOKEN_MAX);
	DEBUG_JMP(DB_USER_INPUT, LINE_DEBUG, COLUMN_FIRST, "User Input: Extracted command %s from 0x%x to 0x%x\n", command, user_input_buffer, str);
	
	if(strcmp(command, "tr") == 0 || strcmp(command, "rv") == 0 || strcmp(command, "sw") == 0) {
		str = str2token(str, token, USER_COMMAND_TOKEN_MAX);
		unsigned char number = atoi(token, 10);
		
		DEBUG_JMP(DB_USER_INPUT, LINE_DEBUG, COLUMN_FIRST, "User Input: Arg1 0x%x\n", number);
		str = str2token(str, token, USER_COMMAND_TOKEN_MAX);
		unsigned char value = 0;
		switch(command[0]) {
			case 'r':
			case 't':
				value = (command[0] == 'r') ? TRAIN_REVERSE : atoi(token, 10);
				DEBUG_JMP(DB_TRAIN_CTRL, LINE_DEBUG, COLUMN_FIRST, "#%u Speed %u\n", number, value);
				break;
			case 's':
				value = (token[0] == 'S') ? SWITCH_STR : SWITCH_CUR;
				DEBUG_JMP(DB_TRAIN_CTRL, LINE_DEBUG, COLUMN_FIRST, "#%d Direct %s\n", number, token);
				break;
		}
		pushTrainCommand(value, TRAIN_COMMAND_DELAY, FALSE);
		pushTrainCommand(number, FALSE, FALSE);
		if(value == 15 || value == 31) {
			pushTrainCommand(25, TRAIN_REVERSE_DELAY, FALSE);
			pushTrainCommand(number, FALSE, FALSE);
		}
		pushTrainCommand(SWITCH_OFF, TRAIN_COMMAND_DELAY, FALSE); // Turn off the solenoid
		
		return 0;
	}
	
	return -1;
}

int handleUserInput() {
	char user_input_char = '\0';
	if(plgetc(COM2, &user_input_char) > 0) {
		
		// Push or pop char from user_input_buffer
		if(user_input_char == ASCI_BACKSPACE && user_input_size > 0){
			user_input_size--;
			user_input_buffer[user_input_size] = '\0';
			printAsciControl(COM2, ASCI_CURSOR_TO, LINE_USER_INPUT, user_input_size + 1);
			printAsciControl(COM2, ASCI_CLEAR_TO_EOL, NO_ARG, NO_ARG);
		}
		else if(user_input_char != ASCI_BACKSPACE && user_input_size < (USER_INPUT_MAX - 1)) {
			user_input_buffer[user_input_size] = user_input_char;
			user_input_size++;
			user_input_buffer[user_input_size] = '\0';
			printAsciControl(COM2, ASCI_CURSOR_TO, LINE_USER_INPUT, user_input_size);
			plputc(COM2, user_input_char);
		}
		else if(user_input_char != '\n' && user_input_char != '\r'){
			return -1;
		}
		
		// If is EOL or buffer full
		if(user_input_char == '\n' || user_input_char == '\r' || user_input_size == USER_INPUT_MAX) {
			// Clear the input line
			printAsciControl(COM2, ASCI_CURSOR_TO, LINE_USER_INPUT, COLUMN_FIRST);
			printAsciControl(COM2, ASCI_CLEAR_TO_EOL, NO_ARG, NO_ARG);
			
			DEBUG_JMP(DB_USER_INPUT, LINE_DEBUG, COLUMN_FIRST, "User Input: Reach EOL. Input Size %u, value %s\n", user_input_size, user_input_buffer);
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
	int i, j;
	for(i = 0; i < SENSOR_DECODER_TOTAL; i++) {
		sensor_decoder_ids[i] = 'A' + i;
		for(j = 0; j < SENSOR_BYTE_EACH; j++) {
			sensor_decoder_data[i * SENSOR_BYTE_EACH + j] = 0x00;
		}
	}
	sensor_request_cts = TRUE;

	DEBUG_JMP(DB_SENSOR, LINE_DEBUG, COLUMN_FIRST, "Sensor: Booting\n");
	while((!getRegisterBit(UART1_BASE, UART_FLAG_OFFSET, CTS_MASK)) || 
		  (getRegisterBit(UART1_BASE, UART_FLAG_OFFSET, TXFF_MASK)) /*|| 
		  (!getRegisterBit(UART1_BASE, UART_FLAG_OFFSET, RXFE_MASK))*/) {
		plputc(COM2, '.');
		char c;
		if(plgetc(COM1, &c) > 0) {
			DEBUG(DB_SENSOR, "Sensor: Consuming sensor data 0x%x\n", c);
		}
		plsend(COM2); // Send debug message chars
	}
	pushTrainCommand(SENSOR_AUTO_RESET, TRAIN_COMMAND_DELAY, FALSE);
}

void receivedSensorData() {
	train_commands_pause_time = FALSE;
	sensor_request_cts = TRUE;
}

void requestSensorData(){
	sensor_request_cts = FALSE;
	sensor_request_time = 0;
	
	int decoder_index = sensor_decoder_next / SENSOR_BYTE_EACH;
	sensor_decoder_next = decoder_index * SENSOR_BYTE_EACH;
	char command = SENSOR_READ_ONE + (sensor_decoder_next / SENSOR_BYTE_EACH) + 1;
	pushTrainCommand(command, TRAIN_COMMAND_DELAY, TRAIN_COMMAND_PAUSE_TIMEOUT);
	DEBUG_JMP(DB_SENSOR, LINE_DEBUG + SENSOR_DECODER_TOTAL * SENSOR_BYTE_EACH + 1, COLUMN_SENSOR_DEBUG, "Req %d\n", command);
}

void pushRecentSensor(char decoder_id, unsigned int sensor_id, unsigned int value) {	
	printAsciControl(COM2, ASCI_CURSOR_TO, LINE_RECENT_SENSOR, COLUMN_FIRST + sensor_recent_next * COLUMN_SENSOR_WIDTH);
	plprintf(COM2, "|%c%d    ", decoder_id, sensor_id);
	sensor_recent_next = (sensor_recent_next + 1) % SENSOR_RECENT_TOTAL;
	printAsciControl(COM2, ASCI_CURSOR_TO, LINE_RECENT_SENSOR, COLUMN_FIRST + sensor_recent_next * COLUMN_SENSOR_WIDTH);
	plprintf(COM2, "|-Next- ");
}

void saveDecoderData(unsigned int decoder_index, char new_data) {
	// Save to sensor_decoder_data
	char old_data = sensor_decoder_data[decoder_index];
	sensor_decoder_data[decoder_index] = new_data;
	
	// If changed
	if(new_data && old_data != new_data) {
	// if(new_data) {
		DEBUG_JMP(DB_SENSOR, LINE_DEBUG + decoder_index, COLUMN_SENSOR_DEBUG, "%c%d: 0x%x\n", sensor_decoder_ids[decoder_index / 2], decoder_index % 2, new_data);
		
		char decoder_id = sensor_decoder_ids[decoder_index / 2];
		char old_temp = old_data;
		char new_temp = new_data;
		
		// Found which sensor changed
		int i;
		unsigned int old_bit, new_bit;
		for(i = 0; i < SENSOR_BYTE_SIZE; i++) {
			old_bit = old_temp & SENSOR_BIT_MASK;
			new_bit = new_temp & SENSOR_BIT_MASK;
			// If changed
			if(new_bit && old_bit != new_bit) {
				int sensor_id = (SENSOR_BYTE_SIZE * (decoder_index % 2)) + (SENSOR_BYTE_SIZE - i);
				DEBUG_JMP(DB_SENSOR, LINE_DEBUG - 1, COLUMN_SENSOR_DEBUG, "#%c%d %x -> %x\n", sensor_decoder_ids[decoder_index / 2], sensor_id, 0 /*old_bit*/, new_bit);
				
				pushRecentSensor(decoder_id, sensor_id, new_bit);
			}
			old_temp = old_temp >> 1;
			new_temp = new_temp >> 1;
		}
	}
}

void collectSensorData(int tick_elapsed) {
	char new_data = '\0';
	if(plgetc(COM1, &new_data) > 0) {
		DEBUG_JMP(DB_TRAIN_CTRL, LINE_DEBUG - 1, COLUMN_FIRST, "Data In %d     \n", sensor_decoder_next);
		// Save the data
		saveDecoderData(sensor_decoder_next, new_data);
		
		// Increment the counter
		sensor_decoder_next = (sensor_decoder_next + 1) % (SENSOR_DECODER_TOTAL * SENSOR_BYTE_EACH);
		DEBUG_JMP(DB_SENSOR, LINE_DEBUG + SENSOR_DECODER_TOTAL * SENSOR_BYTE_EACH, COLUMN_SENSOR_DEBUG, "N %d\n", sensor_decoder_next);
		
		// If end receiving last chunk of data, clear to send sensor data request
		if((sensor_decoder_next % 2) == 0) {
			receivedSensorData();
			DEBUG_JMP(DB_TRAIN_CTRL, LINE_DEBUG - 1, COLUMN_FIRST, "Continue   ");
		}
	}
	
	if(sensor_request_cts == FALSE) {
		sensor_request_time += tick_elapsed;
	} 
	// Request for another chunk of data
	if(sensor_request_cts == TRUE || sensor_request_time > SENSOR_REQUEST_TIMEOUT) {
		if(sensor_request_time > SENSOR_REQUEST_TIMEOUT) DEBUG_JMP(DB_SENSOR, LINE_DEBUG - 1, COLUMN_FIRST, "Restart %d", train_commands_pause_time);
		requestSensorData();
	}
}

/* 
 * Main Polling Loop
 */
void pollingLoop() {
	/* Initialize Elapsed time tracker */
	previous_timer_value = getTimerValue(TIMER3_BASE);
	timer_value_remained = 0;
	timer_tick = 0;
	
	/* Initialize Train Command Buffer */
	train_commands_save_index = 0;
	train_commands_send_index = 0;
	train_commands_pause_time = 0;
	
	/* Initialize User Input Buffer */
	user_input_size = 0;
	user_input_buffer[user_input_size] = '\0';
	
	sensor_decoder_next = 0;
	sensor_recent_next = 0;
	
	/* Initialize Sensor Data Request */
	sensorBootstrap();
	
	/* Polling loop */
	while(TRUE) {
		
		/* Polling IO: Give it a chance to send out char */
		plsend(COM1);
		plsend(COM2);
		
		/* Timer: Calculate and display time elapsed */
		unsigned int tick_elapsed = handleTimeElapse();
		
		/* Try to pop train commands from the buffer */
		popTrainCommand(tick_elapsed);
		
		/* Sensor: Collect and display data */
		collectSensorData(tick_elapsed);
		
		/* User Input */
		if(handleUserInput() == USER_COMMAND_QUIT) break;
	}
}

int main(int argc, char* argv[]) {
	
	/* Initialize Global Variables */
	char plio_buffer[CHANNEL_COUNT * OUTPUT_BUFFER_SIZE];
	unsigned int plio_send_index[CHANNEL_COUNT];
	unsigned int plio_save_index[CHANNEL_COUNT];
	dbflags = 0 /* DB_TRAIN_CTRL | DB_IO | DB_TIMER | DB_USER_INPUT | DB_SENSOR */; // Debug Flags
	
	/* Initialize IO: setup buffer; BOTH: turn off fifo; COM1: speed to 2400, enable stp2 */
	plbootstrap(plio_buffer, plio_send_index, plio_save_index);
	plsetfifo(COM2, OFF);
	plsetfifo(COM1, OFF);
	plsetspeed(COM1, 2400);
	setRegisterBit(UART1_BASE, UART_LCRH_OFFSET, STP2_MASK, TRUE);
	
	// Clear the screen
	printAsciControl(COM2, ASCI_CLEAR_SCREEN, NO_ARG, NO_ARG);
	
	/* Verifiying COM1's Configuration: nothing when debug flag is turned off */
	DEBUG_JMP(DB_IO, LINE_DEBUG, COLUMN_FIRST, "COM1 LCRH: 0x%x\n", getRegister(UART1_BASE, UART_LCRH_OFFSET)); // 0x68
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
