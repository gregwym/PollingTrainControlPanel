/*
 * plio.h
 */

#ifndef __PLIO_H__
#define __PLIO_H__

#ifndef __VA_LIST_H__
#define __VA_LIST_H__

typedef char *va_list;

#define __va_argsiz(t)	\
		(((sizeof(t) + sizeof(int) - 1) / sizeof(int)) * sizeof(int))

#define va_start(ap, pN) ((ap) = ((va_list) __builtin_next_arg(pN)))

#define va_end(ap)	((void)0)

#define va_arg(ap, t)	\
		 (((ap) = (ap) + __va_argsiz(t)), *((t*) (void*) ((ap) - __va_argsiz(t))))

#define COM1	0
#define COM2	1

#define ON	1
#define	OFF	0

#endif // __VA_LIST_H__

#define CHANNEL_COUNT	2
#define OUTPUT_BUFFER_SIZE 10000

void plstat();

void plbootstrap();

void plflush( int channel );

/* 
 * Try to send out a char
 * Return: -1 Unknown Channel, 0 Nothing Sent, 1 Sent
 */
int plsend( int channel );

int plsetfifo( int channel, int state );

int plsetspeed( int channel, int speed );

/* 
 * Put a char into the buffer
 * Return: -1 Unknown Channel, 0 No more space in the buffer, 1 Saved
 */
int plputc( int channel, char c );

int plgetc( int channel, char *c );

int plputx( int channel, char c );

int plputstr( int channel, char *str );

int plputr( int channel, unsigned int reg );

void plputw( int channel, int n, char fc, char *bf );

void plprintf( int channel, char *format, ... );

void plui2a( unsigned int num, unsigned int base, char *bf );

void pli2a( int num, char *bf );

#endif // __PLIO_H__
