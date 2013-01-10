#
# Makefile for busy-wait IO library
#
XCC     = gcc
AS	= as
AR	= ar
CFLAGS  = -c -fPIC -Wall -I. -I../include -mcpu=arm920t -msoft-float
# -g: include hooks for gdb
# -c: only compile
# -mcpu=arm920t: generate code for the 920t architecture
# -fpic: emit position-independent code
# -Wall: report all warnings
# -msoft-float: use software for floating point

ASFLAGS	= -mcpu=arm920t -mapcs-32
# -mapcs-32: always create a complete stack frame

ARFLAGS = rcs

all:  bwio.a plio.a

bwio.s: bwio.c 
	$(XCC) -S $(CFLAGS) bwio.c

bwio.o: bwio.s
	$(AS) $(ASFLAGS) -o bwio.o bwio.s

bwio.a: bwio.o
	$(AR) $(ARFLAGS) $@ bwio.o

plio.s: plio.c 
	$(XCC) -S $(CFLAGS) plio.c

plio.o: plio.s
	$(AS) $(ASFLAGS) -o plio.o plio.s

plio.a: plio.o
	$(AR) $(ARFLAGS) $@ plio.o


clean:
	-rm -f *.s *.a *.o
