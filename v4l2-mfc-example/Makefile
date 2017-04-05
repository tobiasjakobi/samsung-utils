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

optflags := -O2 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
compiler := gcc
cflags   := -Wall -I/usr/include/libdrm -I/usr/include/exynos
ldflags  := -lm -pthread -ldrm
destdir  := /usr/local

ifndef platform
platform := $(shell $(compiler) -dumpmachine)
endif

ifeq (release-lto,$(build))
cflags += $(optflags) -flto=4 -fuse-linker-plugin -DNDEBUG
ldflags += $(optflags) -flto=4 -fuse-linker-plugin
endif

ifeq (release,$(build))
cflags += $(optflags) -DNDEBUG
endif

ifeq (debug,$(build))
cflags += -O0 -g
endif

ifneq (,$(DESTDIR))
destdir := $(DESTDIR)
endif

objects := v4l2_decode

all: $(objects)

%.o: %.c
	$(compiler) -c -o $@ $(cflags) $<

v4l2_decode: main.o fileops.o args.o parser.o fb.o fimc.o mfc.o queue.o drm.o gem.o ipp.o; $(compiler) -o $@ $^ $(ldflags)

clean:
	rm -f *.o
	rm -f $(objects)

strip:
	strip -s $(objects)

install: v4l2_decode
	cp v4l2_decode $(destdir)/bin/
