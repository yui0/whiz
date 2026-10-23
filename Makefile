# whiz — Japanese input method (Wayland / GTK 3/4 / XIM) + CostNet runtime
# 公開版 Makefile: 学習済み資産 engine/out/{model.wcnn,whiz.web.dic} を使う

ROOT       := $(abspath .)
ENGINE     := $(ROOT)/engine
OUT        := $(ENGINE)/out
MODEL      := $(OUT)/model.wcnn
WEB_DIC    := $(OUT)/whiz.web.dic

CC       ?= cc
PYTHON   ?= python3
PREFIX   ?= /usr
DESTDIR  ?=
CFLAGS   ?= -Os -Wall -Wextra
CPPFLAGS ?=
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
LUNA_UI_DIR      ?= $(ROOT)/luna-ui
WAYLAND_PKGS     := wayland-client wayland-egl egl gl xkbcommon
WAYLAND_IM       := whiz-im-wayland

XIM_IM          := whiz-xim
XCB_XIM_PKGS    := xcb-imdkit xkbcommon-x11

SPECFILE     ?= whiz.spec
RPM_NAME     := $(shell rpmspec -q --srpm --qf '%{name}\n' $(SPECFILE) 2>/dev/null | head -n1)
RPM_VERSION  := $(shell rpmspec -q --srpm --qf '%{version}\n' $(SPECFILE) 2>/dev/null | head -n1)
RPM_RELEASE  := $(shell rpmspec -q --srpm --qf '%{release}\n' $(SPECFILE) 2>/dev/null | head -n1)
ifeq ($(strip $(RPM_NAME)),)
RPM_NAME := $(shell awk '/^Name:/{print $$2; exit}' $(SPECFILE))
endif
ifeq ($(strip $(RPM_VERSION)),)
RPM_VERSION := $(shell TZ=Asia/Tokyo date +%Y%m%d)
endif
ifeq ($(strip $(RPM_RELEASE)),)
RPM_RELEASE := $(shell awk '/^Release:/{print $$2; exit}' $(SPECFILE))
endif
RPMBUILD_DIR := $(CURDIR)/rpmbuild

.PHONY: all help cli im im3 im4 im-wayland im-xim test-ime wasm web serve test \
	install install-wayland install-gtk install-im install-im3 install-im4 install-im-xim install-assets \
	update-im-cache update-im3-cache update-im4-cache uninstall clean rpm check-assets

all: cli

help:
	@echo "Targets:"
	@echo "  cli         engine/whiz ネイティブCLIをビルド"
	@echo "  im          GTK 3/4 IMモジュールを両方ビルド"
	@echo "  im3         GTK 3 IMモジュール libim-whiz-gtk3.so をビルド"
	@echo "  im4         GTK 4 IMモジュール libim-whiz-gtk4.so をビルド"
	@echo "  im-xim      X11/XIM サーバー whiz-xim をビルド (XIM クライアント用)"
	@echo "  im-wayland  Luna Desktop用 Wayland IMEをビルド (GTK/Qt不要)"
	@echo "  test-ime    Wayland IMEのローマ字合成セルフテスト"
	@echo "  wasm        Web版 whiz.js/whiz.wasm をビルド (emsdk が必要)"
	@echo "  serve       Web版を http://localhost:8080/whiz.html で配信"
	@echo "  test        CLIで変換を確認"
	@echo "  install     Wayland IME + GTK 3/4 IMと実行時資産を PREFIX=$(PREFIX) に配置"
	@echo "  install-wayland  Wayland IMEと実行時資産を配置"
	@echo "  install-gtk      GTK 3/4 IMと実行時資産を配置しキャッシュを更新"
	@echo "  install-im-xim   XIM サーバーと実行時資産を配置"
	@echo "  rpm         Wayland IME + GTK IM のバイナリ RPM を作成"

check-assets:
	@test -f "$(MODEL)" || { echo "error: missing $(MODEL)"; exit 1; }
	@test -f "$(WEB_DIC)" || { echo "error: missing $(WEB_DIC)"; exit 1; }

cli:
	$(MAKE) -C $(ENGINE) whiz CC="$(CC)" CFLAGS="$(CFLAGS)"

im: im3 im4

im3: libim-whiz-gtk3.so

libim-whiz-gtk3.so: im.c im-candidate-ui.h romaji.h engine/whiz.c
	@pkg-config --exists gtk+-3.0 || \
		{ echo "error: GTK 3 development files are required"; exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC -o $@ im.c \
		$$(pkg-config --cflags --libs gtk+-3.0) -lm

im4: libim-whiz-gtk4.so

libim-whiz-gtk4.so: im4.c im-candidate-ui.h romaji.h engine/whiz.c
	@pkg-config --exists gtk4 || \
		{ echo "error: GTK 4 development files are required"; exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC -o $@ im4.c \
		$$(pkg-config --cflags --libs gtk4) -lm

im-xim: $(XIM_IM)

$(XIM_IM): whiz-xim.c romaji.h engine/whiz.c
	@pkg-config --exists $(XCB_XIM_PKGS) || \
		{ echo "error: xcb-imdkit and xkbcommon-x11 development files are required"; exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-DWHIZ_MODEL_PATH=\"$(DATADIR)/model.wcnn\" \
		-DWHIZ_DIC_PATH=\"$(DATADIR)/whiz.web.dic\" -o $@ \
		whiz-xim.c $$(pkg-config --cflags --libs $(XCB_XIM_PKGS)) -lm

im-wayland: $(WAYLAND_IM)

test-ime: $(WAYLAND_IM)
	./$(WAYLAND_IM) --self-test

$(LUNA_UI_DIR)/luna-ui.h:
	@echo "error: luna-ui not found in $(LUNA_UI_DIR); run 'git submodule update --init'"; exit 1

$(WAYLAND_IM): im-wayland.c romaji.h engine/whiz.c im-wayland.html im-wayland.css \
		protocol/input-method-unstable-v2-client-protocol.h \
		protocol/input-method-unstable-v2-protocol.c \
		protocol/virtual-keyboard-unstable-v1-client-protocol.h \
		protocol/virtual-keyboard-unstable-v1-protocol.c \
		$(LUNA_UI_DIR)/luna-ui.h
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

wasm: check-assets
	$(MAKE) -C $(ENGINE) wasm

web: wasm

serve: wasm
	cd $(ENGINE) && $(PYTHON) -m http.server 8080

test: cli check-assets
	$(ENGINE)/whiz -m "$(MODEL)" -d "$(WEB_DIC)" -p "きょうはいいてんきです"

install-assets: check-assets
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

install-gtk: install-im

update-im3-cache:
	@test -n "$(GTK3_QUERY)" || { echo "error: gtk-query-immodules-3.0 not found"; exit 1; }
	install -d "$(dir $(GTK3_CACHE))"
	"$(GTK3_QUERY)" > "$(GTK3_CACHE)"

update-im4-cache:
	@test -n "$(GIO_QUERYMODULES)" || { echo "error: gio-querymodules not found"; exit 1; }
	"$(GIO_QUERYMODULES)" "$(GTK4_IMMODULE_DIR)"

update-im-cache: update-im3-cache update-im4-cache

install-im-xim: im-xim install-assets
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 "$(XIM_IM)" "$(DESTDIR)$(BINDIR)/$(XIM_IM)"

install-wayland: im-wayland install-assets
	install -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(DATADIR)"
	install -m 0755 "$(WAYLAND_IM)" "$(DESTDIR)$(BINDIR)/$(WAYLAND_IM)"
	install -m 0644 im-wayland.html im-wayland.css "$(DESTDIR)$(DATADIR)/"

install: install-wayland install-im

uninstall:
	rm -f "$(DESTDIR)$(DATADIR)/model.wcnn" \
		"$(DESTDIR)$(DATADIR)/whiz.web.dic" \
		"$(DESTDIR)$(DATADIR)/im-wayland.html" \
		"$(DESTDIR)$(DATADIR)/im-wayland.css" \
		"$(DESTDIR)$(BINDIR)/$(WAYLAND_IM)" \
		"$(DESTDIR)$(BINDIR)/$(XIM_IM)" \
		"$(DESTDIR)$(GTK3_IMMODULE_DIR)/whiz.so" \
		"$(DESTDIR)$(GTK3_ABI_IMMODULE_DIR)/libim-whiz.so" \
		"$(DESTDIR)$(GTK4_IMMODULE_DIR)/libwhiz.so" \
		"$(DESTDIR)$(GTK4_IMMODULE_DIR)/whiz.so" \
		"$(DESTDIR)$(GTK4_IMMODULE_DIR)/libim-whiz.so"

rpm: check-assets
	@command -v rpmbuild >/dev/null || { echo 'rpmbuild is required (dnf install rpm-build)'; exit 1; }
	rm -rf $(RPMBUILD_DIR)
	mkdir -p $(RPMBUILD_DIR)/BUILD $(RPMBUILD_DIR)/BUILDROOT \
	         $(RPMBUILD_DIR)/RPMS $(RPMBUILD_DIR)/SOURCES \
	         $(RPMBUILD_DIR)/SPECS $(RPMBUILD_DIR)/SRPMS
	cp -f $(SPECFILE) $(RPMBUILD_DIR)/SPECS/
	rpmbuild -bb --build-in-place \
	  --define "_topdir $(RPMBUILD_DIR)" \
	  $(RPMBUILD_DIR)/SPECS/$(notdir $(SPECFILE))
	@find $(RPMBUILD_DIR)/RPMS -name '*.rpm' -exec cp -f {} . \;
	@echo "✓ RPM:"
	@find . -maxdepth 1 -name '$(RPM_NAME)-$(RPM_VERSION)-$(RPM_RELEASE).*.rpm' -print

clean:
	$(MAKE) -C $(ENGINE) clean
	rm -f libim-whiz-gtk3.so libim-whiz-gtk4.so $(WAYLAND_IM) $(XIM_IM)
	rm -rf $(RPMBUILD_DIR)
	rm -f $(RPM_NAME)-$(RPM_VERSION)-$(RPM_RELEASE).*.rpm
