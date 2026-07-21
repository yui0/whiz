# apps/whiz — CostNet CLI / WebAssembly / GTK input methods

ROOT       := $(abspath .)
ENGINE     := $(ROOT)/engine
OUT        := $(ENGINE)/out
MODEL      := $(OUT)/model.wcnn
WEB_DIC    := $(OUT)/whiz.web.dic
SOURCE_DIC := $(ROOT)/dic/whiz.dic

CC       ?= cc
PYTHON   ?= python3
PREFIX   ?= /usr
DESTDIR  ?=
CFLAGS   ?= -Os -Wall -Wextra
CPPFLAGS ?=
EMSDK_ENV ?= /root/image/emsdk/emsdk_env.sh
GTK3_LIBDIR         := $(shell pkg-config --variable=libdir gtk+-3.0 2>/dev/null)
GTK3_BINARY_VERSION := $(shell pkg-config --variable=gtk_binary_version gtk+-3.0 2>/dev/null)
GTK3_IMMODULE_DIR   ?= $(GTK3_LIBDIR)/gtk-3.0/immodules
GTK3_ABI_IMMODULE_DIR := $(GTK3_LIBDIR)/gtk-3.0/$(GTK3_BINARY_VERSION)/immodules
GTK3_CACHE          ?= $(GTK3_LIBDIR)/gtk-3.0/$(GTK3_BINARY_VERSION)/immodules.cache
GTK3_QUERY          ?= $(shell command -v gtk-query-immodules-3.0-64 2>/dev/null || command -v gtk-query-immodules-3.0 2>/dev/null)
GTK4_LIBDIR         := $(shell pkg-config --variable=libdir gtk4 2>/dev/null)
GTK4_BINARY_VERSION := $(shell pkg-config --variable=gtk_binary_version gtk4 2>/dev/null)
GTK4_IMMODULE_DIR   ?= $(GTK4_LIBDIR)/gtk-4.0/$(GTK4_BINARY_VERSION)/immodules
GIO_QUERYMODULES    ?= $(shell command -v gio-querymodules-64 2>/dev/null || command -v gio-querymodules 2>/dev/null)
DATADIR          ?= $(PREFIX)/share/whiz
BINDIR           ?= $(PREFIX)/bin
LUNA_UI_DIR      ?= $(abspath ../luna-ui/ui)
WAYLAND_PKGS     := wayland-client wayland-egl egl gl xkbcommon
WAYLAND_IM       := whiz-im-wayland

.PHONY: all help cli im im3 im4 im-wayland test-ime train smoke assets wasm web serve test check \
	install install-wayland install-gtk install-im install-im3 install-im4 install-assets \
	update-im-cache update-im3-cache update-im4-cache uninstall clean

all: cli

help:
	@echo "Targets:"
	@echo "  cli       engine/whiz ネイティブCLIをビルド"
	@echo "  im        GTK 3/4 IMモジュールを両方ビルド"
	@echo "  im3       GTK 3 IMモジュール libim-whiz-gtk3.so をビルド"
	@echo "  im4       GTK 4 IMモジュール libim-whiz-gtk4.so をビルド"
	@echo "  im-wayland  Luna Desktop用 Wayland IMEをビルド (GTK/Qt不要)"
	@echo "  test-ime  Wayland IMEのローマ字合成セルフテスト"
	@echo "  train     CostNetを学習 (PRESET=quick|tiny|base)"
	@echo "  smoke     Python教師/学習済みモデルを検査"
	@echo "  assets    model.wcnn と whiz.web.dic を生成"
	@echo "  wasm      Web版 whiz.js/whiz.wasm をビルド"
	@echo "  serve     Web版を http://localhost:8080/whiz.html で配信"
	@echo "  test      構文・ビルドと、資産があれば変換を検査"
	@echo "  install   Wayland IMEと実行時資産を PREFIX=$(PREFIX) に配置"
	@echo "  install-gtk  GTK 3/4 IMと実行時資産を配置しキャッシュを更新"

cli:
	$(MAKE) -C $(ENGINE) whiz CC="$(CC)" CFLAGS="$(CFLAGS)"

im: im3 im4

im3: libim-whiz-gtk3.so

libim-whiz-gtk3.so: im.c romaji.h conversion.h engine/whiz.c
	@pkg-config --exists gtk+-3.0 || \
		{ echo "error: GTK 3 development files are required"; exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC -o $@ im.c \
		$$(pkg-config --cflags --libs gtk+-3.0) -lm

im4: libim-whiz-gtk4.so

libim-whiz-gtk4.so: im4.c romaji.h conversion.h engine/whiz.c
	@pkg-config --exists gtk4 || \
		{ echo "error: GTK 4 development files are required"; exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC -o $@ im4.c \
		$$(pkg-config --cflags --libs gtk4) -lm

im-wayland: $(WAYLAND_IM)

test-ime: $(WAYLAND_IM)
	./$(WAYLAND_IM) --self-test

$(WAYLAND_IM): im-wayland.c romaji.h conversion.h engine/whiz.c im-wayland.html im-wayland.css \
		protocol/input-method-unstable-v2-client-protocol.h \
		protocol/input-method-unstable-v2-protocol.c \
		protocol/virtual-keyboard-unstable-v1-client-protocol.h \
		protocol/virtual-keyboard-unstable-v1-protocol.c \
		$(LUNA_UI_DIR)/luna-ui.h $(LUNA_UI_DIR)/cssparser.h \
		$(LUNA_UI_DIR)/stb_truetype.h $(LUNA_UI_DIR)/stb_image.h \
		$(LUNA_UI_DIR)/stb_image_write.h
	@pkg-config --exists $(WAYLAND_PKGS) || \
		{ echo "error: Wayland/EGL/OpenGL/xkbcommon development files are required"; exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(LUNA_UI_DIR) -Iprotocol \
		-DWHIZ_UI_SOURCE_DIR=\"$(ROOT)\" \
		-DWHIZ_UI_INSTALL_DIR=\"$(DATADIR)\" \
		-DWHIZ_MODEL_PATH=\"$(DATADIR)/model.wcnn\" \
		-DWHIZ_DIC_PATH=\"$(DATADIR)/whiz.web.dic\" -o $@ \
		im-wayland.c protocol/input-method-unstable-v2-protocol.c \
		protocol/virtual-keyboard-unstable-v1-protocol.c \
		$$(pkg-config --cflags --libs $(WAYLAND_PKGS)) -lm

train:
	cd $(ENGINE) && $(PYTHON) whiz.py train --preset $(or $(PRESET),tiny)

smoke:
	cd $(ENGINE) && $(PYTHON) whiz.py smoke

assets:
	@test -f "$(SOURCE_DIC)" || \
		{ echo "error: missing $(SOURCE_DIC)"; exit 1; }
	@test -f "$(OUT)/connect_net.pt" || \
		{ echo "error: run 'make train' first"; exit 1; }
	cd $(ENGINE) && $(PYTHON) whiz.py export-web

wasm: assets
	EMSDK_ENV="$(EMSDK_ENV)" $(MAKE) -C $(ENGINE) wasm

web: wasm

serve: wasm
	cd $(ENGINE) && $(PYTHON) -m http.server 8080

check: cli
	$(PYTHON) -m py_compile $(ENGINE)/whiz.py

test: check
	@if test -f "$(MODEL)" && test -f "$(SOURCE_DIC)"; then \
		$(ENGINE)/whiz -m "$(MODEL)" -d "$(SOURCE_DIC)" \
			-p "きょうはABCカタカナみとうろく"; \
	else \
		echo "runtime assets not present; native/Python build checks passed"; \
	fi

install-assets: assets
	install -d "$(DESTDIR)$(DATADIR)"
	install -m 0644 "$(MODEL)" "$(DESTDIR)$(DATADIR)/model.wcnn"
	install -m 0644 "$(WEB_DIC)" "$(DESTDIR)$(DATADIR)/whiz.web.dic"

install-im3: im3 install-assets
	@test -n "$(GTK3_LIBDIR)" || { echo "error: cannot determine GTK 3 libdir"; exit 1; }
	install -d "$(DESTDIR)$(GTK3_IMMODULE_DIR)"
	install -m 0755 libim-whiz-gtk3.so "$(DESTDIR)$(GTK3_IMMODULE_DIR)/whiz.so"
	rm -f "$(DESTDIR)$(GTK3_ABI_IMMODULE_DIR)/libim-whiz.so"
	@if test -z "$(DESTDIR)"; then $(MAKE) update-im3-cache; fi

install-im4: im4 install-assets
	@test -n "$(GTK4_LIBDIR)" || { echo "error: cannot determine GTK 4 libdir"; exit 1; }
	install -d "$(DESTDIR)$(GTK4_IMMODULE_DIR)"
	install -m 0755 libim-whiz-gtk4.so "$(DESTDIR)$(GTK4_IMMODULE_DIR)/libwhiz.so"
	rm -f "$(DESTDIR)$(GTK4_IMMODULE_DIR)/libim-whiz.so" \
		"$(DESTDIR)$(GTK4_IMMODULE_DIR)/whiz.so"
	@if test -z "$(DESTDIR)"; then $(MAKE) update-im4-cache; fi

install-im: install-im3 install-im4

update-im3-cache:
	@test -n "$(GTK3_QUERY)" || { echo "error: gtk-query-immodules-3.0 not found"; exit 1; }
	install -d "$(dir $(GTK3_CACHE))"
	"$(GTK3_QUERY)" > "$(GTK3_CACHE)"

update-im4-cache:
	@test -n "$(GIO_QUERYMODULES)" || { echo "error: gio-querymodules not found"; exit 1; }
	"$(GIO_QUERYMODULES)" "$(GTK4_IMMODULE_DIR)"

update-im-cache: update-im3-cache update-im4-cache

install-wayland: im-wayland install-assets
	install -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(DATADIR)"
	install -m 0755 "$(WAYLAND_IM)" "$(DESTDIR)$(BINDIR)/$(WAYLAND_IM)"
	install -m 0644 im-wayland.html im-wayland.css "$(DESTDIR)$(DATADIR)/"

install: install-wayland

install-gtk: install-im

uninstall:
	rm -f "$(DESTDIR)$(DATADIR)/model.wcnn" \
		"$(DESTDIR)$(DATADIR)/whiz.web.dic" \
		"$(DESTDIR)$(DATADIR)/im-wayland.html" \
		"$(DESTDIR)$(DATADIR)/im-wayland.css" \
		"$(DESTDIR)$(BINDIR)/$(WAYLAND_IM)" \
		"$(DESTDIR)$(GTK3_IMMODULE_DIR)/whiz.so" \
		"$(DESTDIR)$(GTK3_ABI_IMMODULE_DIR)/libim-whiz.so" \
		"$(DESTDIR)$(GTK4_IMMODULE_DIR)/libwhiz.so" \
		"$(DESTDIR)$(GTK4_IMMODULE_DIR)/whiz.so" \
		"$(DESTDIR)$(GTK4_IMMODULE_DIR)/libim-whiz.so"

clean:
	$(MAKE) -C $(ENGINE) clean
	rm -f libim-whiz-gtk3.so libim-whiz-gtk4.so $(WAYLAND_IM)
