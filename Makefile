CC = gcc
CFLAGS += -Wall -Wextra -O2
LDFLAGS += -lusb-1.0

TARGET = dcpd
SRC = dcpd.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

