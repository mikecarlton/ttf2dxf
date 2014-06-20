CFLAGS=$(shell freetype-config --cflags) -g -Wall -std=gnu99 -O2
LIBS=$(shell freetype-config --libs) -lm
prefix ?= /usr/local

all: ttf2dxf

ttf2dxf: ttf2dxf.c
	gcc $(CFLAGS) -o $@ $< $(LIBS)

