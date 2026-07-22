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
TEST_BIN = build/test-weather
SOURCE = src/mk-piclock-weather.c
FRAME_SOURCE = ../weather_frames.c
IO_SOURCE = ../io_helpers.c

.PHONY: all test clean

all: $(BIN)

build:
	mkdir -p build

$(BIN): $(SOURCE) $(FRAME_SOURCE) $(IO_SOURCE) ../weather_frames.h ../io_helpers.h ../compiler_attrs.h | build
	$(CC) $(CPPFLAGS) $(CURL_CFLAGS) $(JSON_CFLAGS) $(CFLAGS) $(SOURCE) $(FRAME_SOURCE) $(IO_SOURCE) \
		$(LDFLAGS) $(CURL_LIBS) $(JSON_LIBS) -lm -o $@

$(TEST_BIN): tests/test_weather.c $(SOURCE) $(FRAME_SOURCE) $(IO_SOURCE) ../weather_frames.h ../io_helpers.h ../compiler_attrs.h tests/sample-geomet.json | build
	$(CC) $(CPPFLAGS) $(CURL_CFLAGS) $(JSON_CFLAGS) $(CFLAGS) \
		-Wno-unused-function tests/test_weather.c $(FRAME_SOURCE) $(IO_SOURCE) \
		$(LDFLAGS) $(CURL_LIBS) $(JSON_LIBS) -lm -o $@

test: $(BIN) $(TEST_BIN)
	./$(TEST_BIN)
	sh -n install.sh uninstall.sh
	@if command -v node >/dev/null 2>&1; then \
		node --check ../web/modules/weather/module.js; \
		node --check ../web/modules/dashboard/module.js; \
	fi
	$(CC) -std=c11 -Wall -Wextra -Werror -I.. -c ../weather_source_store.c \
		-o build/weather-source-store.o
	$(CC) -std=c11 -Wall -Wextra -Werror -I.. -c ../weather_frames.c \
		-o build/weather-frames.o
	$(CC) -std=c11 -Wall -Wextra -Werror -I.. -c ../io_helpers.c \
		-o build/io-helpers.o
	rm -f build/weather-source-store.o build/weather-frames.o build/io-helpers.o

clean:
	rm -rf build
