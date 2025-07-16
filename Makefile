CC = gcc
CFLAGS = -Wall -Wextra -O2 -I.
PREFIX = /usr/local
TARGET = eline


SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -Dm644 eline.h $(DESTDIR)$(PREFIX)/include/eline.h

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)$(PREFIX)/include/eline.h

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean install uninstall
