CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
USE_AESD_CHAR_DEVICE ?= 1
# Buildroot invokes this Makefile with CFLAGS/LDFLAGS set on the command
# line, which unconditionally overrides a plain '=' assignment below. Use
# 'override ... +=' so our required flags (in particular -I, needed to find
# aesd_ioctl.h) survive regardless of what's passed in.
override CFLAGS += -Wall -Wextra -g -pthread -I../aesd-char-driver -DUSE_AESD_CHAR_DEVICE=$(USE_AESD_CHAR_DEVICE)
override LDFLAGS += -pthread

TARGET = aesdsocket
SRCS   = aesdsocket.c
OBJS   = $(SRCS:.c=.o)

.PHONY: all default clean

all default: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-rm -f $(OBJS) $(TARGET)
