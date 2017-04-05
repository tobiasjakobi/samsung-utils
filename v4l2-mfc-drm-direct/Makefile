optflags := -O2 -march=armv7-a -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
compiler := g++
cflags   := -std=c++11 -pthread -Wall -I/usr/include/libdrm -I/usr/include/exynos
ldflags  := -ldl -ldrm_exynos -ldrm -lcairo -pthread
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

objects := v4l2_direct

all: $(objects)

%.o: %.cpp
	$(compiler) -c -o $@ $(cflags) $<

v4l2_direct: cairo_text.o exynos_drm.o input_file.o main.o mfc.o parser.o; $(compiler) -o $@ $^ $(ldflags)

clean:
	rm -f *.o
	rm -f $(objects)

strip:
	strip -s $(objects)

install: v4l2_direct
	cp v4l2_direct $(destdir)/bin/
