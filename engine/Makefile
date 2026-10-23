# Native C/WASM converter for WCNN v5 + WUTF v4 assets.
CC ?= cc
CFLAGS ?= -O2 -Wall
LDFLAGS ?= -lm

whiz: whiz.c
	$(CC) $(CFLAGS) -o $@ whiz.c $(LDFLAGS)

test_whiz_c: whiz.c test_whiz_c.c
	$(CC) $(CFLAGS) -Wno-unused-function -o $@ test_whiz_c.c $(LDFLAGS)

check: whiz test_whiz_c
	./test_whiz_c

wasm:
	./build_wasm.sh

clean:
	rm -f whiz test_whiz_c whiz.js whiz.wasm model.wcnn whiz.web.dic

.PHONY: check clean wasm
