CC ?= cc
PKG_CONFIG ?= pkg-config
CPPFLAGS ?= -I..
CFLAGS ?= -std=c11 -Wall -Wextra -Wformat=2 -Werror -O2
LDFLAGS ?=

CURL_CFLAGS := $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null)
CURL_LIBS := $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null)
JSON_CFLAGS := $(shell $(PKG_CONFIG) --cflags json-c 2>/dev/null)
JSON_LIBS := $(shell $(PKG_CONFIG) --libs json-c 2>/dev/null)

BIN = build/mk-piclock-weather
SOURCE = src/mk-piclock-weather.c
FRAME_SOURCE = ../weather_frames.c
IO_SOURCE = ../io_helpers.c

.PHONY: all clean

all: $(BIN)

build:
	mkdir -p build

$(BIN): $(SOURCE) $(FRAME_SOURCE) $(IO_SOURCE) ../weather_frames.h ../io_helpers.h ../compiler_attrs.h | build
	$(CC) $(CPPFLAGS) $(CURL_CFLAGS) $(JSON_CFLAGS) $(CFLAGS) $(SOURCE) $(FRAME_SOURCE) $(IO_SOURCE) \
		$(LDFLAGS) $(CURL_LIBS) $(JSON_LIBS) -lm -o $@

clean:
	rm -rf build
