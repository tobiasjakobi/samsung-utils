# V4L2 Codec decoding example application
# Kamil Debski <k.debski@samsung.com>
#
# Copyright 2012 - 2015 Samsung Electronics Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#



ifeq ($(LIBDRM_PREFIX),)
$(warning LIBDRM_PREFIX is not set, the build may fail due to lack of libdrm headers )
endif

ifeq ($(KERNELHEADERS_PREFIX),)
$(warning KERNELHEADERS_PREFIX is not set, the build may fail due to lack of kernel headers)
endif

# Toolchain path
CROSS_COMPILE ?= arm-linux-gnueabi-
KERNELHEADERS = -I$(KERNELHEADERS_PREFIX)/include
DRMHEADERS =	-I$(LIBDRM_PREFIX)/include \
		-I$(LIBDRM_PREFIX)/include/libdrm \
		-I$(LIBDRM_PREFIX)/include/exynos \
		-I./
LIBRARIES =	-L$(LIBDRM_PREFIX)/lib/
LIBS =		-ldrm

CC = ${CROSS_COMPILE}gcc
AR = "${CROSS_COMPILE}ar rc"
AR2 = ${CROSS_COMPILE}ranlib make


INCLUDES = $(KERNELHEADERS) $(DRMHEADERS)

#INCLUDES = -I$(KERNELHEADERS)/include

#-I$(TARGETROOT)/usr/include/linux

SOURCES = main.c fileops.c args.c parser.c fb.c fimc.c mfc.c queue.c drm.c gem.c ipp.c
OBJECTS := $(SOURCES:.c=.o)
EXEC = v4l2_decode
CFLAGS = -Wall -g -lm
#-Os

all: $(EXEC)

.c.o:
	$(CC) -c $(CFLAGS) $(INCLUDES) $<

$(EXEC): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJECTS) $(LIBRARIES) $(LIBS) -pthread

clean:
	rm -f *.o $(EXEC)

install:

.PHONY: clean all
