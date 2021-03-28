CMAKE_BUILD_TYPE ?= Release

all: gips

_build:
	mkdir _build

_build/build.ninja:
	cmake -G Ninja -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -S . -B _build

gips: _build/build.ninja src/*
	ninja -C _build

cppcheck:
	cppcheck --std=c99 --std=c++11 --enable=all src

clean:
	rm -rf _build

.PHONY: all clean cppcheck