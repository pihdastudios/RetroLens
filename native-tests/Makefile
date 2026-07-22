CXX ?= g++
CXXFLAGS ?= -O1 -g -std=c++98 -Wall -Wextra -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS ?= -fsanitize=address,undefined
ROOT := ..
JNI := $(ROOT)/app/src/main/jni
TARGET := build/retrolens_tests
SOURCES := tests/test_retrolens.cpp $(JNI)/retrolens_core.cpp $(JNI)/display_probe.cpp $(JNI)/jpeg_encoder.cpp $(JNI)/third_party/picojpeg/picojpeg.c

.PHONY: all test clean
all: $(TARGET)

$(TARGET): $(SOURCES) | build
	$(CXX) $(CXXFLAGS) -I$(JNI) $(SOURCES) $(LDFLAGS) -o $@

build:
	mkdir -p build

test: $(TARGET)
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(TARGET)

clean:
	rm -rf build
