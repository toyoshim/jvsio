CFLAGS  = -V -mmcs51 --model-large --xram-size 0x1800 --xram-loc 0x0000 --code-size 0xec00 --stack-auto --Werror -Isrc --opt-code-speed
CC      = sdcc
OBJS	  = jvsio_host.rel jvsio_node.rel

.PHONY: all clean build

all: $(OBJS)

clean:
	rm *.asm *.lst *.rel *.sym

%.rel: %.c *.h
	$(CC) -c $(CFLAGS) -o $@ $<