CC ?= $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall -Wextra -pthread

TARGET = aesdsocket
SOURCES = aesdsocket.c

all: $(TARGET)
default: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET) *.o

.PHONY: all default clean
