CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
USE_AESD_CHAR_DEVICE ?= 1
CFLAGS = -Wall -Wextra -g -pthread -DUSE_AESD_CHAR_DEVICE=$(USE_AESD_CHAR_DEVICE)
LDFLAGS = -pthread

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
