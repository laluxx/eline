CC := gcc
CFLAGS := -Wall -Wextra -O2 -I. -fPIC -g
TARGET := eline
LIB_NAME := libeline
SOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:.c=.o)
INSTALL_DIR := /usr

all: $(TARGET) $(LIB_NAME).a $(LIB_NAME).so

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

$(LIB_NAME).a: $(OBJECTS)
	ar rcs $@ $(OBJECTS)

$(LIB_NAME).so: $(OBJECTS)
	$(CC) -shared -o $@ $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean remove install uninstall

clean:
	rm -f $(OBJECTS) $(TARGET) $(LIB_NAME).a $(LIB_NAME).so

remove: clean
	rm -f $(TARGET)

install:
	install -d $(INSTALL_DIR)/lib
	install -m 644 $(LIB_NAME).a $(INSTALL_DIR)/lib
	install -m 755 $(LIB_NAME).so $(INSTALL_DIR)/lib
	install -d $(INSTALL_DIR)/include
	install -m 644 *.h $(INSTALL_DIR)/include

uninstall:
	rm -f $(INSTALL_DIR)/lib/$(LIB_NAME).a
	rm -f $(INSTALL_DIR)/lib/$(LIB_NAME).so
	rm -f $(INSTALL_DIR)/include/eline.h


# CC = gcc
# CFLAGS = -Wall -Wextra -O2 -I.
# PREFIX = /usr/local
# TARGET = eline


# SRCS = $(wildcard *.c)
# OBJS = $(SRCS:.c=.o)

# all: $(TARGET)

# $(TARGET): $(OBJS)
# 	$(CC) $(CFLAGS) -o $@ $^

# %.o: %.c %.h
# 	$(CC) $(CFLAGS) -c $<

# install: $(TARGET)
# 	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
# 	install -Dm644 eline.h $(DESTDIR)$(PREFIX)/include/eline.h

# uninstall:
# 	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
# 	rm -f $(DESTDIR)$(PREFIX)/include/eline.h

# clean:
# 	rm -f *.o $(TARGET)

# .PHONY: all clean install uninstall
