
CC       ?= gcc
CFLAGS   ?= -Wall -g
TARGET    = temper-hum-hid

LIBS      = `pkg-config libusb-1.0 libudev --libs` -lm
INCLUDES ?= `pkg-config libusb-1.0 --cflags`

all: clean $(TARGET)

gengetopt:
	gengetopt --file-name=temper-hum-hid-cmd < temper-hum-hid-cmd.ggo

$(TARGET): #gengetopt
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBS) temper-hum-hid-api.c temper-hum-hid-cmd.c temper-hum-hid.c -o $@

install:
	cp temper-hum-hid /usr/bin/

clean:
	rm -f $(TARGET)
