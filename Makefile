CC?=cc
LD=$(CC)

PROJECT=fsq

OBJS=main.o common.o WriteableStream.o ReadableStream.o
VPATH=src

CFLAGS?=-O2

all: $(PROJECT)

$(PROJECT): $(OBJS)
	$(LD) -lc $(LDFLAGS) $(OBJS) -o "$(PROJECT)"

.c.o:
	$(CC) -c -g -Wall -Wconversion -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 $(CFLAGS) src/$*.c

clean:
	rm -f *.o "$(PROJECT)"

install:
	install "$(PROJECT)" "$(PREFIX)/bin"
