ifeq ($(KERNELHEADERS_PREFIX),)
$(warning KERNELHEADERS_PREFIX is not set, the build may fail due to lack of kernel headers)
endif

CROSS_COMPILE ?= arm-linux-gnueabi-
CC=$(CROSS_COMPILE)gcc
OBJS = v4l2-dbuf
CFLAGS += -I$(KERNELHEADERS_PREFIX)/include -std=gnu99 -Wall -O2

all:  $(OBJS)

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

% : %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f *.o
	rm -f $(OBJS)
