CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS =

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
