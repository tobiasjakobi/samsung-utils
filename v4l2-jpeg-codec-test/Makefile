# Target root
TARGETROOT?=
# Toolchain path
TCPATH?=arm-linux-gnueabi-

CC = ${TCPATH}gcc
AR = "${TCPATH}ar rc"
AR2 = ${TCPATH}ranlib make -j4

INCLUDES = -I$(TARGETROOT)/include
SOURCES = test-jpeg.c
OBJECTS := $(SOURCES:.c=.o)
EXEC = test-jpeg
CFLAGS = -Wall -g

all: $(EXEC)

.c.o:
	$(CC) -c $(CFLAGS) $(INCLUDES) $<

$(EXEC): $(OBJECTS)
	$(CC) $(CFLAGS) -g -O0 -o $(EXEC) $(OBJECTS) -lpthread

clean:
	rm -f *.o $(EXEC)

install:


.PHONY: clean all
