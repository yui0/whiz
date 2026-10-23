/*
 * Copyright © 2026 Yuichiro Nakada / Project Whiz
 *
 * Whiz input method for Wayland input-method-unstable-v2.
 * This frontend deliberately has no GTK, Qt, or GLFW dependency.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#define _GNU_SOURCE

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <xkbcommon/xkbcommon.h>

#include "protocol/input-method-unstable-v2-client-protocol.h"
#include "protocol/virtual-keyboard-unstable-v1-client-protocol.h"
//#include "kana_xform.h"

#define LUNA_UI_NO_PLATFORM
#define LUNA_UI_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "../luna-ui/luna-ui.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* luna-ui.h declares this for native hosts.  LUNA_UI_NO_PLATFORM leaves it
 * undefined; input widgets call it from input_mark_dirty(). */
void luna_app_request_redraw(void) {}

#define WHIZ_LIBRARY
#define utf8_decode whiz_engine_utf8_decode
#define utf8_encode whiz_engine_utf8_encode
#include "engine/whiz.c"
#undef utf8_decode
#undef utf8_encode
#include "romaji.h"

#ifndef WHIZ_MODEL_PATH
#define WHIZ_MODEL_PATH "/usr/share/whiz/model.wcnn"
#endif
#ifndef WHIZ_DIC_PATH
#define WHIZ_DIC_PATH "/usr/share/whiz/whiz.web.dic"
#endif
#ifndef WHIZ_UI_SOURCE_DIR
#define WHIZ_UI_SOURCE_DIR "."
#endif
#ifndef WHIZ_UI_INSTALL_DIR
#define WHIZ_UI_INSTALL_DIR "/usr/share/whiz"
#endif

enum {
	POPUP_WIDTH = 300, POPUP_NOTICE_HEIGHT = 96, POPUP_EMPTY_HEIGHT = 84,
	POPUP_COMPACT_HEIGHT = 44, POPUP_CANDIDATE_BASE_HEIGHT = 113,
	POPUP_CANDIDATE_ROW_HEIGHT = 28, POPUP_HUD_HEIGHT = 60,
	TEXT_CAP = 8192,
	CAND_CAP = 20
};

typedef struct {
	char word[256];
	char reading[256];
} Candidate;

typedef struct {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_seat *seat;
	struct zwp_input_method_manager_v2 *manager;
	struct zwp_input_method_v2 *input_method;
	struct zwp_virtual_keyboard_manager_v1 *virtual_manager;
	struct zwp_virtual_keyboard_v1 *virtual_keyboard;
	struct zwp_input_method_keyboard_grab_v2 *keyboard;
	struct wl_surface *surface;
	struct zwp_input_popup_surface_v2 *popup;
	uint32_t compositor_name, seat_name, manager_name, virtual_manager_name;
	uint32_t serial;
	bool pending_active, active, unavailable, japanese, running;

	struct xkb_context *xkb_context;
	struct xkb_keymap *keymap;
	struct xkb_state *xkb_state;
	xkb_mod_index_t shift_mod, ctrl_mod, alt_mod;
	uint32_t modifiers;
	uint32_t *forwarded_keys;
	size_t forwarded_key_count, forwarded_key_capacity;
	char *grab_keymap;
	uint32_t grab_keymap_size;
	char *surrounding_text;
	uint32_t surrounding_cursor, surrounding_anchor;

	EGLDisplay egl_display;
	EGLContext egl_context;
	EGLSurface egl_surface;
	EGLConfig egl_config;
	struct wl_egl_window *egl_window;
	bool luna_ready, egl_ready;
	int popup_height;
	bool popup_resized, popup_mapped;
	bool hud, visible;      /* Kotoeri-style あ/A flash on mode change */
	int64_t hud_until_ms;

	char reading[TEXT_CAP];
	char romaji[TEXT_CAP];
	char converted[TEXT_CAP];
	Candidate candidates[CAND_CAP];
	int candidate_count, selected, page;
	bool converting;
	WhizConversion conversion;
	int ui_panel, ui_mode, ui_reading, ui_status, ui_rule;
	int ui_candidates, ui_empty, ui_message, ui_footer, ui_page, ui_hint;
	int ui_number[10];
	int ui_cand[10], ui_word[10];
} App;

static App app;

static void copy_string(char *dst, size_t cap, const char *src)
{
	if (!cap) return;
	if (!src) src = "";
	size_t n = strlen(src);
	if (n >= cap) n = cap - 1;
	memmove(dst, src, n);
	dst[n] = '\0';
}

static bool append_string(char *dst, size_t cap, const char *src)
{
	size_t n = strlen(dst), add = strlen(src);
	if (n + add >= cap) return false;
	memcpy(dst + n, src, add + 1);
	return true;
}

static bool utf8_backspace(char *text)
{
	size_t n = strlen(text);
	if (!n) return false;
	size_t p = n - 1;
	while (p && (((unsigned char)text[p] & 0xc0U) == 0x80U)) p--;
	text[p] = '\0';
	return true;
}

static uint8_t *load_file(const char *path, long *size_out)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) return NULL;
	if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
	long size = ftell(fp);
	if (size <= 0 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
	uint8_t *data = malloc((size_t)size);
	if (!data || fread(data, 1, (size_t)size, fp) != (size_t)size) {
		free(data);
		fclose(fp);
		return NULL;
	}
	fclose(fp);
	*size_out = size;
	return data;
}

static void init_engine(void)
{
	const char *model = getenv("WHIZ_MODEL");
	const char *dic = getenv("WHIZ_DIC");
	if (!model || !*model) model = WHIZ_MODEL_PATH;
	if (!dic || !*dic) dic = WHIZ_DIC_PATH;
	long model_size = 0, dic_size = 0;
	uint8_t *model_data = load_file(model, &model_size);
	uint8_t *dic_data = load_file(dic, &dic_size);
	if (!model_data || !dic_data ||
	    model_size > INT32_MAX || dic_size > INT32_MAX ||
	    !whiz_init(model_data, (int)model_size, dic_data, (int)dic_size))
		fprintf(stderr, "whiz-im-wayland: cannot load model '%s' and dictionary '%s'\n",
		        model, dic);
	free(model_data);
	free(dic_data);
}

static void resolve_final_n(void)
{
	whiz_romaji_convert(app.romaji, app.reading, sizeof(app.reading), true);
}

static void clear_candidates(void)
{
	app.candidate_count = 0;
	app.selected = -1;
	app.page = 0;
	app.converting = false;
	app.converted[0] = '\0';
	whiz_conv_clear(&app.conversion);
}

static void reset_composition(void)
{
	app.reading[0] = '\0';
	app.romaji[0] = '\0';
	clear_candidates();
}

static void parse_predictions(const char *raw)
{
	app.candidate_count = 0;
	if (!raw) return;
	const char *p = raw;
	while (*p && app.candidate_count < CAND_CAP) {
		const char *tab = strchr(p, '\t');
		const char *nl = strchr(p, '\n');
		const char *end = nl ? nl : p + strlen(p);
		if (tab && tab < end) {
			Candidate *c = &app.candidates[app.candidate_count++];
			size_t wn = (size_t)(tab - p), rn = (size_t)(end - tab - 1);
			if (wn >= sizeof(c->word)) wn = sizeof(c->word) - 1;
			if (rn >= sizeof(c->reading)) rn = sizeof(c->reading) - 1;
			memcpy(c->word, p, wn); c->word[wn] = '\0';
			memcpy(c->reading, tab + 1, rn); c->reading[rn] = '\0';
		}
		if (!nl) break;
		p = nl + 1;
	}
	/*
	 * Predictions are suggestions, not an implicit conversion.  Keep the
	 * reading in the preedit until the user explicitly selects a candidate;
	 * this also makes Enter commit hiragana as expected.
	 */
	app.selected = -1;
}

static void predict(void)
{
	clear_candidates();
	if (!app.reading[0] || !whiz_is_ready()) return;
	parse_predictions(whiz_predict(app.reading, CAND_CAP));
}

static void sync_conversion_candidates(void)
{
	app.candidate_count = 0;
	app.selected = -1;
	if (!app.converting || app.conversion.active < 0) return;
	WhizConversionSegment *segment =
		&app.conversion.segments[app.conversion.active];
	for (int i = 0; i < segment->candidate_count && i < CAND_CAP; i++) {
		copy_string(app.candidates[i].word, sizeof(app.candidates[i].word),
		            segment->candidates[i]);
		copy_string(app.candidates[i].reading, sizeof(app.candidates[i].reading),
		            segment->reading);
		app.candidate_count++;
	}
	app.selected = segment->selected;
	app.page = app.selected >= 0 ? app.selected / 10 : 0;
	whiz_conv_compose(&app.conversion, app.converted, sizeof(app.converted));
}

static void convert_reading(void)
{
	if (!app.reading[0]) return;
	resolve_final_n();
	app.converting = whiz_conv_begin(&app.conversion, app.reading);
	sync_conversion_candidates();
}

static void send_transaction(const char *commit, const char *preedit)
{
	if (!app.input_method || !app.active) return;
	if (commit) zwp_input_method_v2_commit_string(app.input_method, commit);
	if (preedit) {
		int32_t cursor = (int32_t)strlen(preedit);
		zwp_input_method_v2_set_preedit_string(app.input_method, preedit, cursor, cursor);
	}
	zwp_input_method_v2_commit(app.input_method, app.serial);
}

static void send_preedit(void)
{
	const char *text = app.reading;
	if (app.converting) {
		whiz_conv_compose(&app.conversion, app.converted, sizeof(app.converted));
		text = app.converted;
	} else if (app.selected >= 0 && app.selected < app.candidate_count) {
		text = app.candidates[app.selected].word;
	}
	send_transaction(NULL, text);
}

static void commit_text(const char *text)
{
	if (!text || !*text) return;
	send_transaction(text, "");
	reset_composition();
}

static void set_hidden(int idx, bool hidden)
{
	if (idx < 0) return;
	if (hidden) luna_add_class(idx, "hidden");
	else luna_remove_class(idx, "hidden");
	luna_update_element_style(idx);
}

static void set_popup_height(int height)
{
	if (height == app.popup_height) return;
	app.popup_height = height;
	if (app.egl_window)
		wl_egl_window_resize(app.egl_window, POPUP_WIDTH, height, 0, 0);
	if (app.luna_ready)
		luna_resize((float)POPUP_WIDTH, (float)height);
	app.popup_resized = true;
}

static int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void flash_mode(void)
{
	app.hud = true;
	app.hud_until_ms = now_ms() + 1000;
}

static void update_ui(void)
{
	if (!app.luna_ready) return;
	const char *display;
	if (app.converting) {
		whiz_conv_compose(&app.conversion, app.converted, sizeof(app.converted));
		display = app.conversion.active >= 0 ?
			app.conversion.segments[app.conversion.active].reading : app.converted;
	} else {
		display = app.reading[0] ? app.reading :
			(app.japanese ? "日本語入力" : "直接入力");
	}
	luna_set_text(app.ui_mode, app.japanese ? "あ" : "A");
	bool hud_expired = app.hud && now_ms() >= app.hud_until_ms;
	if (hud_expired) app.hud = false;
	luna_set_text(app.ui_reading, display);
	bool engine_error = !whiz_is_ready();
	char conversion_status[32];
	snprintf(conversion_status, sizeof(conversion_status), "文節 %d/%d",
	         app.conversion.active + 1, app.conversion.segment_count);
	luna_set_text(app.ui_status, app.unavailable ? "UNAVAILABLE" :
	              (engine_error ? "NO MODEL" :
	               (app.converting ? conversion_status : "予測")));
	luna_set_text(app.ui_hint, app.converting ?
		"↑↓ 選択　Enter 確定　Esc 戻る\n←→ 文節　PgUp/PgDn ページ" :
		"↑↓ / Tab 選択　Enter 確定\nSpace 変換　Esc 戻る");
	bool have = app.candidate_count > 0;
	bool idle = !app.reading[0] && !have && !app.converting;
	bool hud = app.hud && idle;
	bool compact = !app.reading[0] && !have && !engine_error;
	bool notice = !app.reading[0] && !have && engine_error;
	/* Kotoeri shows the mode glyph only briefly; idle Japanese mode is invisible. */
	app.visible = hud || (app.japanese && !compact);
	if (hud) luna_add_class(app.ui_panel, "hud");
	else luna_remove_class(app.ui_panel, "hud");
	if (compact) luna_add_class(app.ui_panel, "compact");
	else luna_remove_class(app.ui_panel, "compact");
	if (notice) {
		luna_add_class(app.ui_panel, "notice");
		luna_add_class(app.ui_message, "notice-message");
	} else {
		luna_remove_class(app.ui_panel, "notice");
		luna_remove_class(app.ui_message, "notice-message");
	}
	bool empty = !compact && !have && !engine_error;
	if (empty) luna_add_class(app.ui_panel, "empty");
	else luna_remove_class(app.ui_panel, "empty");
	luna_update_element_style(app.ui_panel);
	luna_update_element_style(app.ui_message);
	set_hidden(app.ui_reading, hud);
	set_hidden(app.ui_status, hud);
	set_hidden(app.ui_rule, hud || compact || notice);
	set_hidden(app.ui_footer, hud || compact || notice || !have);
	set_hidden(app.ui_candidates, hud || !have);
	set_hidden(app.ui_empty, hud || compact || have || engine_error);
	set_hidden(app.ui_message, hud || !engine_error);
	if (!compact && !have && !engine_error)
		luna_set_text(app.ui_empty, "候補はありません　Spaceで変換します");
	if (engine_error)
		luna_set_text(app.ui_message,
			"モデルまたは辞書を読み込めません\nWHIZ_MODEL / WHIZ_DIC を確認してください");
	int first = app.page * 10;
	for (int row = 0; row < 10; row++) {
		int ci = first + row;
		bool visible = ci < app.candidate_count;
		set_hidden(app.ui_cand[row], !visible);
		if (visible) {
			luna_set_text(app.ui_word[row], app.candidates[ci].word);
			char number[4] = "";
			if (app.converting) snprintf(number, sizeof(number), "%d", (row + 1) % 10);
			luna_set_text(app.ui_number[row], number);
			if (ci == app.selected) luna_add_class(app.ui_cand[row], "selected");
			else luna_remove_class(app.ui_cand[row], "selected");
			luna_update_element_style(app.ui_cand[row]);
		}
	}
	char page[32];
	int pages = app.candidate_count ? (app.candidate_count + 9) / 10 : 1;
	snprintf(page, sizeof(page), "%d / %d", app.page + 1, pages);
	luna_set_text(app.ui_page, page);
	int visible_rows = app.candidate_count - first;
	if (visible_rows < 1) visible_rows = 1;
	if (visible_rows > 10) visible_rows = 10;
	set_popup_height(hud ? POPUP_HUD_HEIGHT : compact ? POPUP_COMPACT_HEIGHT :
	                 (notice ? POPUP_NOTICE_HEIGHT :
	                  (have ? POPUP_CANDIDATE_BASE_HEIGHT +
	                          visible_rows * POPUP_CANDIDATE_ROW_HEIGHT
	                        : POPUP_EMPTY_HEIGHT)));
	luna_mark_layout_dirty();
}

static void render(void)
{
	if (!app.egl_ready || !app.luna_ready) return;
	/*
	 * Never attach a NULL buffer: after a Mesa wl_egl_window unmap the next
	 * swap does not reliably re-map the popup, so candidates never reappeared.
	 * Idle is drawn as a fully transparent frame instead; the compositor
	 * only shows input popups while a text input is active.
	 */
	if (!app.active) return;
	eglMakeCurrent(app.egl_display, app.egl_surface, app.egl_surface, app.egl_context);
	glViewport(0, 0, POPUP_WIDTH, app.popup_height);
	glClearColor(0.f, 0.f, 0.f, 0.f);
	glClear(GL_COLOR_BUFFER_BIT);
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	double now = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
	if (app.visible) {
		luna_update(now, 0.0);
		luna_render(POPUP_WIDTH, app.popup_height);
	}
	eglSwapBuffers(app.egl_display, app.egl_surface);
	app.popup_mapped = true;
}

static void refresh(void)
{
	update_ui();
	render();
	/* Mesa commits the old-sized back buffer on the first swap after a
	 * wl_egl_window resize. Draw once more into the newly sized buffer. */
	if (app.popup_resized) {
		app.popup_resized = false;
		render();
	}
}

static bool begin_reconversion(void)
{
	if (!app.input_method || !app.active || !whiz_is_ready() ||
	    !app.surrounding_text || app.reading[0] || app.romaji[0] ||
	    app.converting) return false;

	size_t text_len = strlen(app.surrounding_text);
	uint32_t start = app.surrounding_cursor < app.surrounding_anchor
		? app.surrounding_cursor : app.surrounding_anchor;
	uint32_t end = app.surrounding_cursor > app.surrounding_anchor
		? app.surrounding_cursor : app.surrounding_anchor;
	if (start == end || end > text_len || end - start >= TEXT_CAP) return false;

	char selected[TEXT_CAP];
	size_t selected_len = end - start;
	memcpy(selected, app.surrounding_text + start, selected_len);
	selected[selected_len] = '\0';
	const char *reversed = whiz_reverse(selected);
	if (!reversed || !*reversed) return false;

	char reading[TEXT_CAP];
	copy_string(reading, sizeof(reading), reversed);
	if (!whiz_conv_begin(&app.conversion, reading)) return false;

	copy_string(app.reading, sizeof(app.reading), reading);
	app.romaji[0] = '\0';
	app.converting = true;
	sync_conversion_candidates();

	/* Delete the selected client text and replace it with the conversion
	 * preedit in one input-method transaction. Lengths are UTF-8 bytes. */
	uint32_t before = app.surrounding_cursor - start;
	uint32_t after = end - app.surrounding_cursor;
	zwp_input_method_v2_delete_surrounding_text(app.input_method, before, after);
	whiz_conv_compose(&app.conversion, app.converted, sizeof(app.converted));
	int32_t cursor = (int32_t)strlen(app.converted);
	zwp_input_method_v2_set_preedit_string(
		app.input_method, app.converted, cursor, cursor);
	zwp_input_method_v2_commit(app.input_method, app.serial);
	app.surrounding_anchor = app.surrounding_cursor;
	refresh();
	return true;
}

static void bind_ui_ids(void)
{
	app.ui_panel = luna_get_element_by_id("panel");
	app.ui_mode = luna_get_element_by_id("mode_badge");
	app.ui_reading = luna_get_element_by_id("reading");
	app.ui_status = luna_get_element_by_id("status");
	app.ui_rule = luna_get_element_by_id("rule");
	app.ui_candidates = luna_get_element_by_id("candidates");
	app.ui_empty = luna_get_element_by_id("empty");
	app.ui_message = luna_get_element_by_id("message");
	app.ui_footer = luna_get_element_by_id("footer");
	app.ui_page = luna_get_element_by_id("page");
	app.ui_hint = luna_get_element_by_id("hint_primary");
	for (int i = 0; i < 10; i++) {
		char id[16];
		snprintf(id, sizeof(id), "cand%d", i);
		app.ui_cand[i] = luna_get_element_by_id(id);
		snprintf(id, sizeof(id), "word%d", i);
		app.ui_word[i] = luna_get_element_by_id(id);
		snprintf(id, sizeof(id), "num%d", i);
		app.ui_number[i] = luna_get_element_by_id(id);
	}
}

static bool find_ui_assets(char *html, size_t html_cap, char *css, size_t css_cap,
                           char *base, size_t base_cap)
{
	const char *dirs[] = {
		getenv("WHIZ_UI_DIR"), WHIZ_UI_SOURCE_DIR, ".",
		WHIZ_UI_INSTALL_DIR, "/usr/local/share/whiz", "/usr/share/whiz"
	};
	for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
		if (!dirs[i] || !*dirs[i]) continue;
		snprintf(html, html_cap, "%s/im-wayland.html", dirs[i]);
		snprintf(css, css_cap, "%s/im-wayland.css", dirs[i]);
		if (access(html, R_OK) == 0 && access(css, R_OK) == 0) {
			copy_string(base, base_cap, dirs[i]);
			return true;
		}
	}
	return false;
}

static void *egl_proc(const char *name)
{
	__eglMustCastToProperFunctionPointerType p = eglGetProcAddress(name);
	void *result = NULL;
	/* POSIX/OpenGL loaders conventionally permit this representation copy. */
	if (sizeof(result) == sizeof(p)) memcpy(&result, &p, sizeof(result));
	return result;
}

static bool init_graphics(void)
{
	int initial_height = whiz_is_ready() ? POPUP_COMPACT_HEIGHT :
	                                      POPUP_NOTICE_HEIGHT;
	app.egl_display = eglGetDisplay((EGLNativeDisplayType)app.display);
	if (app.egl_display == EGL_NO_DISPLAY ||
	    !eglInitialize(app.egl_display, NULL, NULL) ||
	    !eglBindAPI(EGL_OPENGL_API)) return false;
	const EGLint config_attrs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_NONE
	};
	EGLint count = 0;
	if (!eglChooseConfig(app.egl_display, config_attrs, &app.egl_config, 1, &count) ||
	    count < 1) return false;
	const EGLint context_attrs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
		EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
		EGL_NONE
	};
	app.egl_context = eglCreateContext(app.egl_display, app.egl_config,
	                                   EGL_NO_CONTEXT, context_attrs);
	if (app.egl_context == EGL_NO_CONTEXT) return false;
	app.egl_window = wl_egl_window_create(app.surface, POPUP_WIDTH, initial_height);
	if (!app.egl_window) return false;
	app.egl_surface = eglCreateWindowSurface(app.egl_display, app.egl_config,
	                                         (EGLNativeWindowType)app.egl_window, NULL);
	if (app.egl_surface == EGL_NO_SURFACE ||
	    !eglMakeCurrent(app.egl_display, app.egl_surface,
	                    app.egl_surface, app.egl_context)) return false;
	eglSwapInterval(app.egl_display, 1);
	LunaInitConfig config = {
		.width = (float)POPUP_WIDTH,
		.height = (float)initial_height,
		.get_proc = egl_proc,
		.frameless = 1,
	};
	if (!luna_init(&config)) return false;
	char html[1024], css[1024], base[1024];
	if (!find_ui_assets(html, sizeof(html), css, sizeof(css), base, sizeof(base))) {
		fprintf(stderr, "whiz-im-wayland: im-wayland.html/css not found\n");
		return false;
	}
	luna_set_html_base_dir(base);
	/* Luna computes element styles while parsing HTML, so load rules first. */
	if (!luna_load_css_file(css) || !luna_load_html_file(html)) {
		fprintf(stderr, "whiz-im-wayland: failed to load UI assets from %s\n", base);
		return false;
	}
	luna_inject_body_background();
	luna_resize((float)POPUP_WIDTH, (float)initial_height);
	app.popup_height = initial_height;
	app.luna_ready = true;
	bind_ui_ids();
	app.egl_ready = true;
	return true;
}

static void destroy_keyboard(void)
{
	if (app.keyboard) {
		zwp_input_method_keyboard_grab_v2_release(app.keyboard);
		app.keyboard = NULL;
	}
}

static void destroy_popup(void)
{
	if (app.popup) {
		zwp_input_popup_surface_v2_destroy(app.popup);
		app.popup = NULL;
	}
}

static int keymap_fd_from_bytes(const char *bytes, uint32_t size)
{
	int fd = memfd_create("whiz-virtual-keymap", MFD_CLOEXEC);
	if (fd < 0) {
		char path[] = "/tmp/whiz-virtual-keymap-XXXXXX";
		fd = mkstemp(path);
		if (fd >= 0) {
			unlink(path);
			(void)fcntl(fd, F_SETFD, FD_CLOEXEC);
		}
	}
	if (fd < 0) return -1;
	size_t offset = 0;
	while (offset < size) {
		ssize_t written = write(fd, bytes + offset, size - offset);
		if (written < 0 && errno == EINTR) continue;
		if (written <= 0) { close(fd); return -1; }
		offset += (size_t)written;
	}
	if (lseek(fd, 0, SEEK_SET) < 0) { close(fd); return -1; }
	return fd;
}

static void send_virtual_keymap(void)
{
	if (!app.virtual_keyboard || !app.grab_keymap || !app.grab_keymap_size) return;
	int fd = keymap_fd_from_bytes(app.grab_keymap, app.grab_keymap_size);
	if (fd < 0) {
		fprintf(stderr, "whiz-im-wayland: cannot copy keyboard keymap: %s\n",
		        strerror(errno));
		return;
	}
	zwp_virtual_keyboard_v1_keymap(app.virtual_keyboard,
		ZWP_VIRTUAL_KEYBOARD_V1_KEYMAP_FORMAT_XKB_V1, fd, app.grab_keymap_size);
	close(fd);
}

static void forward_key(uint32_t time, uint32_t key, uint32_t state)
{
	if (!app.virtual_keyboard) return;
	uint32_t virtual_state = state ==
		ZWP_INPUT_METHOD_KEYBOARD_GRAB_V2_KEY_STATE_PRESSED
		? ZWP_VIRTUAL_KEYBOARD_V1_KEY_STATE_PRESSED
		: ZWP_VIRTUAL_KEYBOARD_V1_KEY_STATE_RELEASED;
	zwp_virtual_keyboard_v1_key(app.virtual_keyboard, time, key, virtual_state);
}

static bool track_forwarded_key(uint32_t key)
{
	for (size_t i = 0; i < app.forwarded_key_count; i++)
		if (app.forwarded_keys[i] == key) return true;
	if (app.forwarded_key_count == app.forwarded_key_capacity) {
		size_t capacity = app.forwarded_key_capacity
			? app.forwarded_key_capacity * 2 : 32;
		uint32_t *keys = realloc(app.forwarded_keys, capacity * sizeof(*keys));
		if (!keys) return false;
		app.forwarded_keys = keys;
		app.forwarded_key_capacity = capacity;
	}
	app.forwarded_keys[app.forwarded_key_count++] = key;
	return true;
}

static bool untrack_forwarded_key(uint32_t key)
{
	for (size_t i = 0; i < app.forwarded_key_count; i++) {
		if (app.forwarded_keys[i] != key) continue;
		app.forwarded_keys[i] =
			app.forwarded_keys[--app.forwarded_key_count];
		return true;
	}
	return false;
}

static bool mod_active(xkb_mod_index_t index)
{
	return index != XKB_MOD_INVALID && (app.modifiers & (1U << index));
}

static bool is_toggle(xkb_keysym_t sym)
{
	return sym == XKB_KEY_Zenkaku_Hankaku || sym == XKB_KEY_Kanji ||
	       (sym == XKB_KEY_space && (mod_active(app.shift_mod) || mod_active(app.ctrl_mod)));
}

static void select_relative(int delta)
{
	if (!app.candidate_count) return;
	if (app.converting) {
		whiz_conv_select_candidate(&app.conversion, delta);
		sync_conversion_candidates();
	} else {
		if (app.selected < 0)
			app.selected = delta < 0 ? app.candidate_count - 1 : 0;
		else
			app.selected =
				(app.selected + delta + app.candidate_count) % app.candidate_count;
	}
	app.page = app.selected / 10;
	send_preedit();
	refresh();
}

/* F6–F10: hiragana / katakana / hw-kata / fw-ascii / hw-ascii */
static bool commit_xform(int mode)
{
	if (app.converting) {
		int active = app.conversion.active;
		if (active < 0 || active >= app.conversion.segment_count)
			return false;
		if (mode < WHIZ_XFORM_FW_ASCII) {
			WhizConversionSegment *segment = &app.conversion.segments[active];
			char buf[WHIZ_CONV_WORD];
			if (!whiz_xform_apply(mode, segment->reading, NULL,
			                      buf, sizeof(buf))) return false;
			whiz_conv_copy(segment->candidates[0], WHIZ_CONV_WORD, buf);
			segment->candidate_count = 1;
			segment->selected = 0;
			sync_conversion_candidates();
			send_preedit();
			refresh();
			return true;
		}
		app.converting = false;
		whiz_conv_clear(&app.conversion);
	}
	if (app.romaji[0]) resolve_final_n();
	if (!app.reading[0] && !app.romaji[0]) return false;

	char buf[TEXT_CAP];
	if (!whiz_xform_apply(mode, app.reading, app.romaji, buf, sizeof(buf)))
		return false;
	commit_text(buf);
	refresh();
	return true;
}

static bool handle_key(xkb_keysym_t sym)
{
	/* The dedicated conversion keys select a mode; they are not toggles. */
	if (sym == XKB_KEY_Henkan && !app.japanese) {
		app.japanese = true;
		flash_mode();
		if (begin_reconversion()) return true;
		refresh();
		return true;
	}
	if (sym == XKB_KEY_Muhenkan && !app.japanese)
		return true;

	if (is_toggle(sym)) {
		app.japanese = !app.japanese;
		flash_mode();
		if (!app.japanese) {
			if (app.converting) {
				whiz_conv_compose(&app.conversion, app.converted,
				                  sizeof(app.converted));
			whiz_conv_learn(&app.conversion);
			commit_text(app.converted);
			} else if (app.reading[0]) {
				resolve_final_n();
				commit_text(app.reading);
			}
			else send_preedit();
		}
		refresh();
		return true;
	}
	if (!app.japanese) return false;
	if (sym >= XKB_KEY_F6 && sym <= XKB_KEY_F10)
		return commit_xform((int)(sym - XKB_KEY_F6) + WHIZ_XFORM_HIRAGANA);
	if (sym == XKB_KEY_Muhenkan) {
		if (app.converting || app.reading[0] || app.romaji[0])
			return commit_xform(WHIZ_XFORM_HIRAGANA);
		app.japanese = false;
		flash_mode();
		refresh();
		return true;
	}
	if (sym == XKB_KEY_Escape) {
		if (app.converting) {
			app.converting = false;
			whiz_conv_clear(&app.conversion);
			predict();
		} else {
			reset_composition();
		}
		send_preedit();
		refresh();
		return true;
	}
	/* Delete belongs to the client and must not commit the active conversion. */
	if (sym == XKB_KEY_Delete || sym == XKB_KEY_KP_Delete)
		return false;
	if (sym == XKB_KEY_BackSpace ||
	    (mod_active(app.ctrl_mod) && (sym == XKB_KEY_h || sym == XKB_KEY_H))) {
		if (app.converting) {
			app.converting = false;
			whiz_conv_clear(&app.conversion);
			predict();
		} else if (app.romaji[0]) {
			whiz_romaji_backspace_kana(app.romaji, app.reading,
			                          sizeof(app.reading));
			predict();
		} else if (utf8_backspace(app.reading)) {
			predict();
		} else return false;
		send_preedit();
		refresh();
		return true;
	}
	if (app.converting && (sym == XKB_KEY_Left || sym == XKB_KEY_Right)) {
		int delta = sym == XKB_KEY_Left ? -1 : 1;
		if (mod_active(app.shift_mod))
			whiz_conv_resize(&app.conversion, delta);
		else
			whiz_conv_select_segment(&app.conversion, delta);
		sync_conversion_candidates();
		send_preedit();
		refresh();
		return true;
	}
	if (sym == XKB_KEY_Page_Up || sym == XKB_KEY_Page_Down) {
		if (!app.candidate_count) return false;
		int current = app.selected < 0 ? 0 : app.selected;
		int target = current + (sym == XKB_KEY_Page_Up ? -10 : 10);
		if (target < 0) target = 0;
		if (target >= app.candidate_count) target = app.candidate_count - 1;
		if (app.selected < 0) app.selected = 0;
		select_relative(target - current);
		return true;
	}
	if (sym == XKB_KEY_Up) {
		if (!app.candidate_count) predict();
		if (app.candidate_count) { select_relative(-1); return true; }
		return false;
	}
	if (sym == XKB_KEY_Down) {
		if (!app.candidate_count) predict();
		if (app.candidate_count) { select_relative(1); return true; }
		return false;
	}
	if (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab) {
		if (!app.candidate_count) predict();
		if (app.candidate_count) {
			select_relative((sym == XKB_KEY_ISO_Left_Tab || mod_active(app.shift_mod)) ? -1 : 1);
			return true;
		}
		return false;
	}
	/* During conversion, the numbered rows are direct-selection shortcuts.
	 * Outside conversion, ASCII digits remain literal input. */
	char digit_value = '\0';
	if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9)
		digit_value = (char)('0' + sym - XKB_KEY_0);
	else if (sym >= XKB_KEY_KP_0 && sym <= XKB_KEY_KP_9)
		digit_value = (char)('0' + sym - XKB_KEY_KP_0);
	if (!mod_active(app.ctrl_mod) && !mod_active(app.alt_mod) && digit_value) {
		if (app.converting && app.conversion.active >= 0) {
			int row = digit_value == '0' ? 9 : digit_value - '1';
			int candidate = app.page * 10 + row;
			WhizConversionSegment *segment =
				&app.conversion.segments[app.conversion.active];
			if (candidate < segment->candidate_count) {
				whiz_conv_select_candidate(
					&app.conversion, candidate - segment->selected);
				whiz_conv_compose(&app.conversion, app.converted,
				                  sizeof(app.converted));
			whiz_conv_learn(&app.conversion);
			commit_text(app.converted);
				refresh();
				return true;
			}
		}
		char digit[2] = { digit_value, '\0' };
		if (app.converting) {
			whiz_conv_compose(&app.conversion, app.converted,
			                  sizeof(app.converted));
			whiz_conv_learn(&app.conversion);
			commit_text(app.converted);
		} else if (app.reading[0] || app.romaji[0]) {
			resolve_final_n();
			commit_text(app.reading);
		}
		commit_text(digit);
		refresh();
		return true;
	}
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter ||
	    (mod_active(app.ctrl_mod) && (sym == XKB_KEY_m || sym == XKB_KEY_M))) {
		if (app.converting) {
			whiz_conv_compose(&app.conversion, app.converted,
			                  sizeof(app.converted));
			whiz_conv_learn(&app.conversion);
			commit_text(app.converted);
		} else if (app.selected >= 0 && app.selected < app.candidate_count)
			commit_text(app.candidates[app.selected].word);
		else if (app.reading[0]) {
			resolve_final_n();
			commit_text(app.reading);
		} else return false;
		refresh();
		return true;
	}
	if (sym == XKB_KEY_space || sym == XKB_KEY_Henkan) {
		if (!app.reading[0]) {
			if (sym == XKB_KEY_Henkan && begin_reconversion())
				return true;
			if (sym == XKB_KEY_space)
				commit_text("　");
		} else if (!app.converting) {
			convert_reading();
			send_preedit();
		} else {
			select_relative(1);
			return true;
		}
		refresh();
		return true;
	}
	if (mod_active(app.ctrl_mod) || mod_active(app.alt_mod)) return false;
	char utf8[8] = {0};
	int n = xkb_keysym_to_utf8(sym, utf8, sizeof(utf8));
	/* xkbcommon includes the terminating NUL in this return value. */
	if (n <= 1) return false;
	if (app.converting) {
		whiz_conv_compose(&app.conversion, app.converted, sizeof(app.converted));
			whiz_conv_learn(&app.conversion);
			commit_text(app.converted);
	}
	if (n == 2 && ((utf8[0] >= 'A' && utf8[0] <= 'Z') ||
	              (utf8[0] >= 'a' && utf8[0] <= 'z') ||
	              strchr("-,.[]'", utf8[0]))) {
		char c[2] = { utf8[0], '\0' };
		if (c[0] >= 'A' && c[0] <= 'Z') c[0] = (char)(c[0] - 'A' + 'a');
		append_string(app.romaji, sizeof(app.romaji), c);
		whiz_romaji_convert(app.romaji, app.reading,
		                    sizeof(app.reading), false);
	} else {
		append_string(app.reading, sizeof(app.reading), utf8);
	}
	predict();
	send_preedit();
	refresh();
	return true;
}

static void keyboard_keymap(void *data,
	struct zwp_input_method_keyboard_grab_v2 *object,
	uint32_t format, int32_t fd, uint32_t size)
{
	(void)data; (void)object;
	if (format != ZWP_INPUT_METHOD_KEYBOARD_GRAB_V2_KEYMAP_FORMAT_XKB_V1 ||
	    fd < 0 || size == 0) {
		if (fd >= 0) close(fd);
		return;
	}
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) return;
	char *keymap_copy = malloc(size);
	if (keymap_copy) {
		memcpy(keymap_copy, map, size);
		free(app.grab_keymap);
		app.grab_keymap = keymap_copy;
		app.grab_keymap_size = size;
		send_virtual_keymap();
	}
	struct xkb_keymap *keymap = xkb_keymap_new_from_string(
		app.xkb_context, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map, size);
	if (!keymap) return;
	struct xkb_state *state = xkb_state_new(keymap);
	if (!state) { xkb_keymap_unref(keymap); return; }
	xkb_state_unref(app.xkb_state);
	xkb_keymap_unref(app.keymap);
	app.keymap = keymap;
	app.xkb_state = state;
	app.shift_mod = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
	app.ctrl_mod = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
	app.alt_mod = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
}

static void keyboard_key(void *data,
	struct zwp_input_method_keyboard_grab_v2 *object,
	uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	(void)data; (void)object; (void)serial;
	if (state == ZWP_INPUT_METHOD_KEYBOARD_GRAB_V2_KEY_STATE_RELEASED) {
		if (untrack_forwarded_key(key))
			forward_key(time, key, state);
		return;
	}
	if (state != ZWP_INPUT_METHOD_KEYBOARD_GRAB_V2_KEY_STATE_PRESSED) return;
	bool consumed = false;
	if (app.xkb_state) {
		xkb_keycode_t code = key + 8;
		xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkb_state, code);
		consumed = handle_key(sym);
	}
	if (!consumed && track_forwarded_key(key))
		forward_key(time, key, state);
}

static void keyboard_modifiers(void *data,
	struct zwp_input_method_keyboard_grab_v2 *object,
	uint32_t serial, uint32_t depressed, uint32_t latched,
	uint32_t locked, uint32_t group)
{
	(void)data; (void)object; (void)serial;
	if (app.virtual_keyboard)
		zwp_virtual_keyboard_v1_modifiers(app.virtual_keyboard,
			depressed, latched, locked, group);
	if (!app.xkb_state) return;
	xkb_state_update_mask(app.xkb_state, depressed, latched, locked, 0, 0, group);
	app.modifiers = depressed | latched | locked;
}

static void keyboard_repeat(void *data,
	struct zwp_input_method_keyboard_grab_v2 *object, int32_t rate, int32_t delay)
{
	(void)data; (void)object; (void)rate; (void)delay;
}

static const struct zwp_input_method_keyboard_grab_v2_listener keyboard_listener = {
	keyboard_keymap, keyboard_key, keyboard_modifiers, keyboard_repeat
};

static void popup_rectangle(void *data, struct zwp_input_popup_surface_v2 *object,
	int32_t x, int32_t y, int32_t width, int32_t height)
{
	(void)data; (void)object; (void)x; (void)y; (void)width; (void)height;
}

static const struct zwp_input_popup_surface_v2_listener popup_listener = {
	popup_rectangle
};

static void input_activate(void *data, struct zwp_input_method_v2 *object)
{
	(void)data; (void)object;
	app.pending_active = true;
}

static void input_deactivate(void *data, struct zwp_input_method_v2 *object)
{
	(void)data; (void)object;
	app.pending_active = false;
}

static void input_surrounding(void *data, struct zwp_input_method_v2 *object,
	const char *text, uint32_t cursor, uint32_t anchor)
{
	(void)data; (void)object;
	free(app.surrounding_text);
	app.surrounding_text = NULL;
	app.surrounding_cursor = app.surrounding_anchor = 0;
	if (!text) return;
	size_t len = strlen(text);
	if (cursor > len || anchor > len) return;
	app.surrounding_text = strdup(text);
	if (!app.surrounding_text) return;
	app.surrounding_cursor = cursor;
	app.surrounding_anchor = anchor;
}

static void input_cause(void *data, struct zwp_input_method_v2 *object, uint32_t cause)
{
	(void)data; (void)object; (void)cause;
}

static void input_content(void *data, struct zwp_input_method_v2 *object,
	uint32_t hint, uint32_t purpose)
{
	(void)data; (void)object; (void)hint; (void)purpose;
}

static void input_done(void *data, struct zwp_input_method_v2 *object)
{
	(void)data; (void)object;
	app.serial++;
	if (app.pending_active && !app.active) {
		app.active = true;
		reset_composition();
		if (!app.surface) app.surface = wl_compositor_create_surface(app.compositor);
		if (app.surface && !app.popup) {
			app.popup = zwp_input_method_v2_get_input_popup_surface(app.input_method,
			                                                        app.surface);
			zwp_input_popup_surface_v2_add_listener(app.popup, &popup_listener, &app);
		}
		if (!app.keyboard) {
			app.keyboard = zwp_input_method_v2_grab_keyboard(app.input_method);
			zwp_input_method_keyboard_grab_v2_add_listener(
				app.keyboard, &keyboard_listener, &app);
		}
		if (app.surface && !app.egl_ready && !init_graphics()) {
			fprintf(stderr,
			        "whiz-im-wayland: popup UI unavailable (EGL 0x%x); keyboard IME continues\n",
			        eglGetError());
		}
		refresh();
	} else if (!app.pending_active && app.active) {
		app.active = false;
		reset_composition();
		render();
		/*
		 * Keep both role objects. The compositor makes the popup inert while
		 * inactive; recreating its role on this wl_surface is invalid.
		 */
	}
}

static void input_unavailable(void *data, struct zwp_input_method_v2 *object)
{
	(void)data; (void)object;
	app.unavailable = true;
	app.running = false;
	fprintf(stderr, "whiz-im-wayland: compositor reports input method unavailable\n");
}

static const struct zwp_input_method_v2_listener input_listener = {
	input_activate, input_deactivate, input_surrounding, input_cause,
	input_content, input_done, input_unavailable
};

static void create_input_method(void)
{
	if (app.input_method || !app.manager || !app.seat) return;
	app.input_method = zwp_input_method_manager_v2_get_input_method(app.manager, app.seat);
	zwp_input_method_v2_add_listener(app.input_method, &input_listener, &app);
}

static void create_virtual_keyboard(void)
{
	if (app.virtual_keyboard || !app.virtual_manager || !app.seat) return;
	app.virtual_keyboard =
		zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
			app.virtual_manager, app.seat);
	send_virtual_keymap();
}

static void registry_global(void *data, struct wl_registry *registry,
	uint32_t name, const char *interface, uint32_t version)
{
	(void)data;
	if (!strcmp(interface, wl_compositor_interface.name) && !app.compositor) {
		uint32_t use = version < 4 ? version : 4;
		app.compositor = wl_registry_bind(registry, name, &wl_compositor_interface, use);
		app.compositor_name = name;
	} else if (!strcmp(interface, wl_seat_interface.name) && !app.seat) {
		uint32_t use = version < 7 ? version : 7;
		app.seat = wl_registry_bind(registry, name, &wl_seat_interface, use);
		app.seat_name = name;
		create_input_method();
		create_virtual_keyboard();
	} else if (!strcmp(interface, zwp_input_method_manager_v2_interface.name) &&
	           !app.manager) {
		app.manager = wl_registry_bind(registry, name,
			&zwp_input_method_manager_v2_interface, 1);
		app.manager_name = name;
		create_input_method();
	} else if (!strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) &&
	           !app.virtual_manager) {
		app.virtual_manager = wl_registry_bind(registry, name,
			&zwp_virtual_keyboard_manager_v1_interface, 1);
		app.virtual_manager_name = name;
		create_virtual_keyboard();
	}
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data; (void)registry;
	if (name == app.seat_name || name == app.manager_name ||
	    name == app.virtual_manager_name ||
	    name == app.compositor_name) {
		fprintf(stderr, "whiz-im-wayland: required Wayland global removed\n");
		app.running = false;
	}
}

static const struct wl_registry_listener registry_listener = {
	registry_global, registry_remove
};

static void cleanup(void)
{
	destroy_keyboard();
	destroy_popup();
	if (app.luna_ready) luna_shutdown();
	if (app.egl_display != EGL_NO_DISPLAY) {
		eglMakeCurrent(app.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (app.egl_surface != EGL_NO_SURFACE)
			eglDestroySurface(app.egl_display, app.egl_surface);
		if (app.egl_context != EGL_NO_CONTEXT)
			eglDestroyContext(app.egl_display, app.egl_context);
		eglTerminate(app.egl_display);
	}
	if (app.egl_window) wl_egl_window_destroy(app.egl_window);
	if (app.surface) wl_surface_destroy(app.surface);
	if (app.input_method) zwp_input_method_v2_destroy(app.input_method);
	if (app.virtual_keyboard) zwp_virtual_keyboard_v1_destroy(app.virtual_keyboard);
	if (app.manager) zwp_input_method_manager_v2_destroy(app.manager);
	if (app.virtual_manager)
		zwp_virtual_keyboard_manager_v1_destroy(app.virtual_manager);
	if (app.seat) wl_seat_destroy(app.seat);
	if (app.compositor) wl_compositor_destroy(app.compositor);
	if (app.registry) wl_registry_destroy(app.registry);
	xkb_state_unref(app.xkb_state);
	xkb_keymap_unref(app.keymap);
	xkb_context_unref(app.xkb_context);
	free(app.forwarded_keys);
	free(app.grab_keymap);
	free(app.surrounding_text);
	if (whiz_is_ready()) whiz_free();
	if (app.display) wl_display_disconnect(app.display);
}

static int run_self_test(void)
{
	static const struct {
		const char *romaji;
		const char *hiragana;
	} cases[] = {
		{ "konnnichiha", "こんにちは" },
		{ "hanni", "はんい" },
		{ "han'i", "はんい" },
		{ "hanxi", "はんい" },
		{ "gakkou", "がっこう" },
		{ "nyuuryoku", "にゅうりょく" },
		{ "kanpai", "かんぱい" },
		{ "shashinn", "しゃしん" },
		{ "nna", "\u3093\u3042" },
		{ "nnna", "\u3093\u306a" },
		{ "n'na", "\u3093\u306a" },
		{ "kan", "\u304b\u3093" },
		{ "thi", "\u3066\u3043" },
		{ "vu", "\u3094" },
	};
	char result[256];
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		whiz_romaji_convert(cases[i].romaji, result, sizeof(result), true);
		if (strcmp(result, cases[i].hiragana)) {
			fprintf(stderr, "romaji self-test failed: %s -> %s (expected %s)\n",
			        cases[i].romaji, result, cases[i].hiragana);
			return EXIT_FAILURE;
		}
	}
	puts("whiz-im-wayland: romaji self-test passed");
	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--self-test"))
		return run_self_test();
	memset(&app, 0, sizeof(app));
	app.egl_display = EGL_NO_DISPLAY;
	app.egl_context = EGL_NO_CONTEXT;
	app.egl_surface = EGL_NO_SURFACE;
	app.running = true;
	app.japanese = true;
	app.selected = -1;
	app.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!app.xkb_context) {
		fprintf(stderr, "whiz-im-wayland: xkb context creation failed\n");
		return EXIT_FAILURE;
	}
	init_engine();
	app.display = wl_display_connect(NULL);
	if (!app.display) {
		fprintf(stderr, "whiz-im-wayland: cannot connect to Wayland display: %s\n",
		        strerror(errno));
		cleanup();
		return EXIT_FAILURE;
	}
	app.registry = wl_display_get_registry(app.display);
	wl_registry_add_listener(app.registry, &registry_listener, &app);
	if (wl_display_roundtrip(app.display) < 0 ||
	    wl_display_roundtrip(app.display) < 0) {
		fprintf(stderr, "whiz-im-wayland: registry roundtrip failed\n");
		cleanup();
		return EXIT_FAILURE;
	}
	if (!app.compositor || !app.seat || !app.manager || !app.input_method ||
	    !app.virtual_manager || !app.virtual_keyboard) {
		fprintf(stderr, "whiz-im-wayland: compositor lacks wl_compositor, wl_seat, "
		        "zwp_input_method_manager_v2, or "
		        "zwp_virtual_keyboard_manager_v1\n");
		cleanup();
		return EXIT_FAILURE;
	}
	while (app.running) {
		/* Poll so the mode HUD can expire without further input. */
		while (wl_display_prepare_read(app.display) != 0)
			if (wl_display_dispatch_pending(app.display) < 0) { app.running = false; break; }
		if (!app.running) break;
		wl_display_flush(app.display);
		struct pollfd pfd = { wl_display_get_fd(app.display), POLLIN, 0 };
		int timeout = -1;
		if (app.hud) {
			int64_t left = app.hud_until_ms - now_ms();
			timeout = left > 0 ? (int)left : 0;
		}
		int n = poll(&pfd, 1, timeout);
		if (n > 0) wl_display_read_events(app.display);
		else wl_display_cancel_read(app.display);
		if (n < 0 && errno != EINTR) break;
		if (wl_display_dispatch_pending(app.display) < 0) break;
		if (app.hud && now_ms() >= app.hud_until_ms) refresh();
	}
	bool failed = app.unavailable || app.running;
	cleanup();
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
