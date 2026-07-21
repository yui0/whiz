# Connect-model C converter
CC ?= cc
CFLAGS ?= -Os -Wall
LDFLAGS ?= -lm

whiz: whiz.c
	$(CC) $(CFLAGS) -o $@ whiz.c $(LDFLAGS)

wasm:
	./build_wasm.sh

clean:
	rm -f whiz whiz.js whiz.wasm model.wcnn whiz.web.dic

.PHONY: clean wasm
