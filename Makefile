CC?=cc
LD=$(CC)

PROJECT=buf

OBJS=main.o common.o WStream.o RStream.o
VPATH=src

CFLAGS?=-O2

build: $(PROJECT)

$(PROJECT): $(OBJS)
	$(LD) -lc $(LDFLAGS) $(OBJS) -o "$(PROJECT)"

.c.o:
	$(CC) -c -g -Wall -Wconversion -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 $(CFLAGS) src/$*.c

clean:
	rm -f *.o "$(PROJECT)"

install: build
	install "$(PROJECT)" "$(PREFIX)/bin"

test:
	sh tests/all.sh
