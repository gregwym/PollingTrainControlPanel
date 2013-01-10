#
# Makefile for busy-wait IO tests
#
XCC     = gcc
AS	= as
LD      = ld
CFLAGS  = -c -fPIC -Wall -I. -I./io/include -mcpu=arm920t -msoft-float
# -g: include hooks for gdb
# -c: only compile
# -mcpu=arm920t: generate code for the 920t architecture
# -fpic: emit position-independent code
# -Wall: report all warnings

ASFLAGS	= -mcpu=arm920t -mapcs-32
# -mapcs: always generate a complete stack frame

LDFLAGS = -init main -Map train_control_panel.map -N  -T orex.ld -L/u/wbcowan/gnuarm-4.0.2/lib/gcc/arm-elf/4.0.2 -L./io/lib

all:  train_control_panel.s train_control_panel.elf

train_control_panel.s: train_control_panel.c train_control_panel.h
	$(XCC) -S $(CFLAGS) train_control_panel.c

train_control_panel.o: train_control_panel.s
	$(AS) $(ASFLAGS) -o train_control_panel.o train_control_panel.s

train_control_panel.elf: train_control_panel.o
	$(LD) $(LDFLAGS) -o $@ train_control_panel.o -lplio -lbwio -lgcc

clean:
	-rm -f train_control_panel.elf *.s *.o train_control_panel.map
