/*
 * plio.c - busy-wait I/O routines for diagnosis
 *
 * Specific to the TS-7200 ARM evaluation board
 *
 */

#include <ts7200.h>
#include <plio.h>
#include <bwio.h>

static char *buffer;
static unsigned int *buffer_send_index;
static unsigned int *buffer_save_index;

// static unsigned int max_send_index = 0;
// static unsigned int max_save_index = 0;
static unsigned int total_send[CHANNEL_COUNT];
static unsigned int total_save[CHANNEL_COUNT];

void plstat() {
	int i = 0;
	for(i = 0; i < CHANNEL_COUNT; i++) {
		bwprintf( COM2, "Channel #%d Send total: 0x%x\n", i, total_send[i]);
		bwprintf( COM2, "Channel #%d Save total: 0x%x\n", i, total_save[i]);
	}
	return;
}

void plbootstrap(char *buf_array, unsigned int *buf_send_array, unsigned int *buf_save_array) {
	buffer = buf_array;
	buffer_send_index = buf_send_array;
	buffer_save_index = buf_save_array;
	
	// max_send_index = 0;
	// max_save_index = 0;
	// plstat();
	
	int i, j;
	for(i = 0; i < CHANNEL_COUNT; i++) {
		for(j = 0; j < OUTPUT_BUFFER_SIZE; j++) {
			buffer[(i * OUTPUT_BUFFER_SIZE) + j] = '\0';
		}
		buffer_send_index[i] = 0;
		buffer_save_index[i] = 0;
		
		total_send[i] = 0;
		total_save[i] = 0;
	}
	
	// bwprintf(COM2, "BOOTSTRAP buffer: 0x%x, send_index: 0x%x, save_index: 0x%x\n", buffer, buffer_send_index, buffer_save_index);
	return;
}

void plflush( int channel ) {
	while(plsend(channel) != 0);
}

int plsend( int channel ) {
	if(channel != COM1 && channel != COM2) return -1;
	
	// If something is waiting to be sent
	if(buffer_send_index[channel] != buffer_save_index[channel]) {
		
		unsigned int actual_index = (channel * OUTPUT_BUFFER_SIZE) + buffer_send_index[channel];
		char c = buffer[actual_index];
		int *flags;
		char *data;
		
		switch( channel ) {
			case COM1:
				flags = (int *)( UART1_BASE + UART_FLAG_OFFSET );
				data = (char *)( UART1_BASE + UART_DATA_OFFSET );
				// If COM1 UART not CTS, return
				if( !( *flags & CTS_MASK ) || !( *flags & RXFE_MASK )) return 3;
				break;
			case COM2:
				flags = (int *)( UART2_BASE + UART_FLAG_OFFSET );
				data = (char *)( UART2_BASE + UART_DATA_OFFSET );
				break;
			default:
				return -1;
				break;
		}
		// If UART FIFO full, return
		if( ( *flags & TXFF_MASK ) ) return 2;
		
		*data = c;
		buffer[actual_index] = '\0';
		
		// Stat data
		// if(buffer_send_index[channel] > max_send_index) {
		// 	max_send_index = buffer_send_index[channel];
		// }
		total_send[channel]++;
		
		unsigned int next_index = (buffer_send_index[channel] + 1) % OUTPUT_BUFFER_SIZE;
		buffer_send_index[channel] = next_index;
		
		return 1;
	}
	return 0;
}

int plsave( int channel, char c ) {
	if(channel != COM1 && channel != COM2) return -1;
	unsigned int next_index = (buffer_save_index[channel] + 1) % OUTPUT_BUFFER_SIZE;
	if(next_index != buffer_send_index[channel]) {
		
		unsigned int actual_index = (channel * OUTPUT_BUFFER_SIZE) + buffer_save_index[channel];
		buffer[actual_index] = c;
		
		// Stat data
		// if(buffer_save_index[channel] > max_save_index) {
		// 	max_save_index = buffer_save_index[channel];
		// }
		total_save[channel]++;
		
		buffer_save_index[channel] = next_index;
		
		return 1;
	}
	
	// No more space in the buffer
	bwprintf(COM2, "Polling IO: Channel %d buffer is full", channel);
	return 0;
}

/*
 * The UARTs are initialized by RedBoot to the following state
 * 	115,200 bps
 * 	8 bits
 * 	no parity
 * 	fifos enabled
 */
int plsetfifo( int channel, int state ) {
	int *line, buf;
	switch( channel ) {
	case COM1:
		line = (int *)( UART1_BASE + UART_LCRH_OFFSET );
			break;
	case COM2:
			line = (int *)( UART2_BASE + UART_LCRH_OFFSET );
			break;
	default:
			return -1;
			break;
	}
	buf = *line;
	buf = state ? buf | FEN_MASK : buf & ~FEN_MASK;
	*line = buf;
	return 0;
}

int plsetspeed( int channel, int speed ) {
	int *high, *low;
	switch( channel ) {
	case COM1:
		high = (int *)( UART1_BASE + UART_LCRM_OFFSET );
		low = (int *)( UART1_BASE + UART_LCRL_OFFSET );
			break;
	case COM2:
		high = (int *)( UART2_BASE + UART_LCRM_OFFSET );
		low = (int *)( UART2_BASE + UART_LCRL_OFFSET );
			break;
	default:
			return -1;
			break;
	}
	switch( speed ) {
	case 115200:
		*high = 0x0;
		*low = 0x3;
		return 0;
	case 2400:
		*high = 0x0;
		*low = 0xbf;
		return 0;
	default:
		return -1;
	}
}

int plputc( int channel, char c ) {
	return plsave(channel, c);
}

char plc2x( char ch ) {
	if ( (ch <= 9) ) return '0' + ch;
	return 'a' + ch - 10;
}

int plputx( int channel, char c ) {
	char chh, chl;

	chh = plc2x( c / 16 );
	chl = plc2x( c % 16 );
	plputc( channel, chh );
	return plputc( channel, chl );
}

int plputr( int channel, unsigned int reg ) {
	int byte;
	char *ch = (char *) &reg;

	for( byte = 3; byte >= 0; byte-- ) plputx( channel, ch[byte] );
	return plputc( channel, ' ' );
}

int plputstr( int channel, char *str ) {
	while( *str ) {
		if( plputc( channel, *str ) < 0 ) return -1;
		str++;
	}
	return 0;
}

void plputw( int channel, int n, char fc, char *bf ) {
	char ch;
	char *p = bf;

	while( *p++ && n > 0 ) n--;
	while( n-- > 0 ) plputc( channel, fc );
	while( ( ch = *bf++ ) ) plputc( channel, ch );
}

int plgetc( int channel, char *c ) {
	int *flags, *data;
	// unsigned char c;

	switch( channel ) {
	case COM1:
		flags = (int *)( UART1_BASE + UART_FLAG_OFFSET );
		data = (int *)( UART1_BASE + UART_DATA_OFFSET );
		break;
	case COM2:
		flags = (int *)( UART2_BASE + UART_FLAG_OFFSET );
		data = (int *)( UART2_BASE + UART_DATA_OFFSET );
		break;
	default:
		return -1;
		break;
	}
	if( !( *flags & RXFE_MASK ) ) {
		*c = *data;
		return 1;
	}
	return 0;
}

int pla2d( char ch ) {
	if( ch >= '0' && ch <= '9' ) return ch - '0';
	if( ch >= 'a' && ch <= 'f' ) return ch - 'a' + 10;
	if( ch >= 'A' && ch <= 'F' ) return ch - 'A' + 10;
	return -1;
}

char pla2i( char ch, char **src, int base, int *nump ) {
	int num, digit;
	char *p;

	p = *src; num = 0;
	while( ( digit = pla2d( ch ) ) >= 0 ) {
		if ( digit > base ) break;
		num = num*base + digit;
		ch = *p++;
	}
	*src = p; *nump = num;
	return ch;
}

void plui2a( unsigned int num, unsigned int base, char *bf ) {
	int n = 0;
	int dgt;
	unsigned int d = 1;

	while( (num / d) >= base ) d *= base;
	while( d != 0 ) {
		dgt = num / d;
		num %= d;
		d /= base;
		if( n || dgt > 0 || d == 0 ) {
			*bf++ = dgt + ( dgt < 10 ? '0' : 'a' - 10 );
			++n;
		}
	}
	*bf = 0;
}

void pli2a( int num, char *bf ) {
	if( num < 0 ) {
		num = -num;
		*bf++ = '-';
	}
	plui2a( num, 10, bf );
}

void plformat ( int channel, char *fmt, va_list va ) {
	char bf[12];
	char ch, lz;
	int w;


	while ( ( ch = *(fmt++) ) ) {
		if ( ch != '%' )
			plputc( channel, ch );
		else {
			lz = 0; w = 0;
			ch = *(fmt++);
			switch ( ch ) {
			case '0':
				lz = 1; ch = *(fmt++);
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				ch = pla2i( ch, &fmt, 10, &w );
				break;
			}
			switch( ch ) {
			case 0: return;
			case 'c':
				plputc( channel, va_arg( va, char ) );
				break;
			case 's':
				plputw( channel, w, 0, va_arg( va, char* ) );
				break;
			case 'u':
				plui2a( va_arg( va, unsigned int ), 10, bf );
				plputw( channel, w, lz, bf );
				break;
			case 'd':
				pli2a( va_arg( va, int ), bf );
				plputw( channel, w, lz, bf );
				break;
			case 'x':
				plui2a( va_arg( va, unsigned int ), 16, bf );
				plputw( channel, w, lz, bf );
				break;
			case '%':
				plputc( channel, ch );
				break;
			}
		}
	}
}

void plprintf( int channel, char *fmt, ... ) {
		va_list va;

		va_start(va,fmt);
		plformat( channel, fmt, va );
		va_end(va);
}

