/*
 * Copyright © 2026 Yuichiro Nakada / Project Whiz
 *
 * whiz-xim — XIM server for Whiz Japanese IME
 *
 * XMODIFIERS=@im=whiz を設定した後、このバイナリをバックグラウンドで
 * 起動すると zt などの XIM クライアントから日本語入力が可能になる。
 *
 * ビルド:
 *   cc -Os -o whiz-xim whiz-xim.c \
 *      $(pkg-config --cflags --libs xcb-imdkit xkbcommon-x11) -lm
 *
 * 使い方:
 *   export XMODIFIERS=@im=whiz
 *   export LANG=ja_JP.UTF-8
 *   ./whiz-xim &
 *   ./zt
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <poll.h>

#include <xcb/xcb.h>
#include <xcb-imdkit/imdkit.h>
#include <xcb-imdkit/encoding.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define WHIZ_LIBRARY
#include "engine/whiz.c"
#include "romaji.h"
//#include "kana_xform.h"

#ifndef WHIZ_MODEL_PATH
#define WHIZ_MODEL_PATH "/usr/share/whiz/model.wcnn"
#endif
#ifndef WHIZ_DIC_PATH
#define WHIZ_DIC_PATH   "/usr/share/whiz/whiz.web.dic"
#endif

#define BUFFSIZE 4096

// -------------------------------------------------------
// per-IC 状態
// -------------------------------------------------------
typedef struct {
	char romaji[BUFFSIZE];
	char preedit[BUFFSIZE];
	int  preedit_len;
	char cand[BUFFSIZE];
	int  converting;
	WhizConversion *conversion;
	char pred_words[20][256];
	int  n_pred;
	int  pred_sel;
	int  ja_mode;
	bool preedit_started; // PreeditCallbacks 用フラグ
} WhizIC;

// -------------------------------------------------------
// グローバル
// -------------------------------------------------------
static xcb_connection_t   *g_conn;
static xcb_window_t        g_win;
static xcb_im_t           *g_im;
static struct xkb_context *g_xkb_ctx;
static struct xkb_keymap  *g_xkb_keymap;
static struct xkb_state   *g_xkb_state;
static volatile bool       g_running = true;

static void on_signal(int sig) { (void)sig; g_running = false; }

// -------------------------------------------------------
// Whiz エンジン初期化
// -------------------------------------------------------
static void engine_init(void)
{
	if (whiz_is_ready()) return;

	const char *mpath = getenv("WHIZ_MODEL") ? getenv("WHIZ_MODEL") : WHIZ_MODEL_PATH;
	const char *dpath = getenv("WHIZ_DIC")   ? getenv("WHIZ_DIC")   : WHIZ_DIC_PATH;

	FILE *fp = fopen(mpath, "rb");
	if (!fp) { fprintf(stderr, "whiz-xim: cannot open model: %s\n", mpath); return; }
	fseek(fp, 0, SEEK_END); long msz = ftell(fp); fseek(fp, 0, SEEK_SET);
	uint8_t *mbuf = malloc((size_t)msz);
	if (!mbuf || (long)fread(mbuf, 1, (size_t)msz, fp) != msz) { free(mbuf); fclose(fp); return; }
	fclose(fp);

	fp = fopen(dpath, "rb");
	if (!fp) { free(mbuf); fprintf(stderr, "whiz-xim: cannot open dic: %s\n", dpath); return; }
	fseek(fp, 0, SEEK_END); long dsz = ftell(fp); fseek(fp, 0, SEEK_SET);
	uint8_t *dbuf = malloc((size_t)dsz);
	if (!dbuf || (long)fread(dbuf, 1, (size_t)dsz, fp) != dsz) { free(mbuf); free(dbuf); fclose(fp); return; }
	fclose(fp);

	if (!whiz_init(mbuf, (int)msz, dbuf, (int)dsz))
		fprintf(stderr, "whiz-xim: whiz_init failed\n");
	free(mbuf);
	free(dbuf);
}

// -------------------------------------------------------
// UTF-8 文字数カウント
// -------------------------------------------------------
static uint32_t utf8_char_count(const char *s)
{
	uint32_t n = 0;
	while (*s) {
		if (((unsigned char)*s & 0xc0) != 0x80) n++;
		s++;
	}
	return n;
}

// -------------------------------------------------------
// preedit 表示 (XIMPreeditCallbacks 対応クライアント用)
// -------------------------------------------------------
static void preedit_update(xcb_im_input_context_t *ic, WhizIC *wic, const char *text)
{
	uint32_t style = xcb_im_input_context_get_input_style(ic);
	bool has_cb = (style & XCB_IM_PreeditCallbacks) != 0;
	if (!has_cb) return;

	if (!text || !*text) {
		if (wic->preedit_started) {
			xcb_im_preedit_done_callback(g_im, ic);
			wic->preedit_started = false;
		}
		return;
	}

	size_t clen = 0;
	char *ct = xcb_utf8_to_compound_text(text, strlen(text), &clen);
	if (!ct) return;

	if (!wic->preedit_started) {
		xcb_im_preedit_start_callback(g_im, ic);
		wic->preedit_started = true;
	}

	xcb_im_preedit_draw_fr_t frame = {0};
	frame.caret                   = utf8_char_count(text);
	frame.chg_first               = 0;
	frame.chg_length              = frame.caret;
	frame.status                  = 0; /* no bitmap */
	frame.length_of_preedit_string = (uint16_t)clen;
	frame.preedit_string           = (uint8_t *)ct;
	xcb_im_preedit_draw_callback(g_im, ic, &frame);
	free(ct);
}


// -------------------------------------------------------
// テキストをクライアントへコミット
// -------------------------------------------------------
static void commit_utf8(xcb_im_input_context_t *ic, WhizIC *wic, const char *utf8)
{
	if (!utf8 || !*utf8) return;

	/* preedit を終了してからコミット */
	if (wic->preedit_started) {
		xcb_im_preedit_done_callback(g_im, ic);
		wic->preedit_started = false;
	}

	size_t clen = 0;
	char *ct = xcb_utf8_to_compound_text(utf8, strlen(utf8), &clen);
	if (ct) {
		xcb_im_commit_string(g_im, ic, XCB_XIM_LOOKUP_CHARS, ct, (uint32_t)clen, 0);
		free(ct);
	}
}

// -------------------------------------------------------
// IC 状態リセット
// -------------------------------------------------------
static void reset_ic(xcb_im_input_context_t *ic, WhizIC *wic)
{
	preedit_update(ic, wic, NULL);
	wic->romaji[0]   = '\0';
	wic->preedit[0]  = '\0';
	wic->preedit_len = 0;
	wic->cand[0]     = '\0';
	wic->converting  = 0;
	wic->n_pred      = 0;
	wic->pred_sel    = -1;
	whiz_conv_clear(wic->conversion);
}

// -------------------------------------------------------
// 予測変換
// -------------------------------------------------------
static void do_predict(WhizIC *wic)
{
	wic->n_pred   = 0;
	wic->pred_sel = -1;
	if (wic->preedit_len == 0 || wic->converting || !whiz_is_ready()) return;
	const char *raw = whiz_predict(wic->preedit, 10);
	if (!raw) return;
	char buf[4096];
	strncpy(buf, raw, sizeof(buf) - 1);
	buf[sizeof(buf)-1] = '\0';
	char *line = buf;
	while (*line && wic->n_pred < 20) {
		char *nl  = strchr(line, '\n');
		if (nl) *nl = '\0';
		char *tab = strchr(line, '\t');
		if (tab) {
			*tab = '\0';
			strncpy(wic->pred_words[wic->n_pred], line, 255);
			wic->pred_words[wic->n_pred][255] = '\0';
			wic->n_pred++;
		}
		line = nl ? nl + 1 : line + strlen(line);
	}
}

// -------------------------------------------------------
// キーイベント処理 (im.c の imcontext_filter_keypress に相当)
// -------------------------------------------------------
static bool is_modekey(xkb_keysym_t sym, uint16_t state)
{
	if (sym == XKB_KEY_Zenkaku_Hankaku) return true;
	if (sym == XKB_KEY_Kanji)           return true;
	if ((state & XCB_MOD_MASK_SHIFT)   && sym == XKB_KEY_space) return true;
	if ((state & XCB_MOD_MASK_CONTROL) && sym == XKB_KEY_space) return true;
	return false;
}

/* F6–F10: commit as hiragana / katakana / hw-kata / fw-ascii / hw-ascii */
static bool commit_xform(xcb_im_input_context_t *ic, WhizIC *wic, int mode)
{
	if (wic->converting) {
		wic->converting = 0;
		wic->cand[0] = '\0';
		whiz_conv_clear(wic->conversion);
	}
	if (wic->romaji[0]) {
		whiz_romaji_convert(wic->romaji, wic->preedit,
		                    sizeof(wic->preedit), true);
		wic->preedit_len = (int)strlen(wic->preedit);
	}
	if (wic->preedit_len == 0 && !wic->romaji[0]) return false;

	char buf[BUFFSIZE];
	if (!whiz_xform_apply(mode, wic->preedit, wic->romaji, buf, sizeof(buf)))
		return false;
	commit_utf8(ic, wic, buf);
	reset_ic(ic, wic);
	return true;
}

/* イベントを消費したら true、クライアントへ戻す場合は false */
static bool handle_key(xcb_im_input_context_t *ic, WhizIC *wic,
                       xcb_key_press_event_t *kev)
{
	/* XKB ステートを X11 モディファイア状態から更新 */
	xkb_state_update_mask(g_xkb_state, kev->state, 0, 0, 0, 0, 0);
	xkb_keysym_t sym = xkb_state_key_get_one_sym(g_xkb_state, kev->detail);

	/* --- モード切替 --- */
	if (is_modekey(sym, kev->state)) {
		wic->ja_mode = !wic->ja_mode;
		if (!wic->ja_mode) {
			if (wic->converting) {
				whiz_conv_compose(wic->conversion, wic->cand, sizeof(wic->cand));
			whiz_conv_learn(wic->conversion);
			commit_utf8(ic, wic, wic->cand);
			} else if (wic->preedit_len > 0) {
				whiz_romaji_convert(wic->romaji, wic->preedit,
				                    sizeof(wic->preedit), true);
				commit_utf8(ic, wic, wic->preedit);
			}
			reset_ic(ic, wic);
		}
		return true;
	}

	/* --- EN モード: そのままクライアントへ --- */
	if (!wic->ja_mode) return false;

	/* --- F6–F10: ひらがな / カタカナ / ｶﾀｶﾅ / 全角英数 / 半角英数 --- */
	if (sym >= XKB_KEY_F6 && sym <= XKB_KEY_F10)
		return commit_xform(ic, wic,
		                    (int)(sym - XKB_KEY_F6) + WHIZ_XFORM_HIRAGANA);

	/* --- ESC / 無変換 --- */
	if (sym == XKB_KEY_Escape || sym == XKB_KEY_Muhenkan) {
		if (wic->converting) {
			wic->converting = 0;
			wic->cand[0] = '\0';
			whiz_conv_clear(wic->conversion);
			preedit_update(ic, wic, wic->preedit);
			return true;
		}
		if (wic->preedit_len > 0) {
			reset_ic(ic, wic);
			return true;
		}
		return false;
	}

	/* --- BackSpace --- */
	if (sym == XKB_KEY_BackSpace ||
	    ((kev->state & XCB_MOD_MASK_CONTROL) && sym == XKB_KEY_h)) {
		if (wic->converting) {
			wic->converting = 0;
			wic->cand[0] = '\0';
			whiz_conv_clear(wic->conversion);
			preedit_update(ic, wic, wic->preedit);
			return true;
		}
		if (wic->preedit_len > 0) {
			whiz_romaji_backspace_kana(wic->romaji, wic->preedit,
			                           sizeof(wic->preedit));
			wic->preedit_len = (int)strlen(wic->preedit);
			do_predict(wic);
			preedit_update(ic, wic, wic->preedit);
			return true;
		}
		return false;
	}

	/* --- Return / Ctrl+m --- */
	if (sym == XKB_KEY_Return ||
	    ((kev->state & XCB_MOD_MASK_CONTROL) && sym == XKB_KEY_m)) {
		if (wic->pred_sel >= 0 && wic->n_pred > 0 && !wic->converting) {
			commit_utf8(ic, wic, wic->pred_words[wic->pred_sel]);
			reset_ic(ic, wic);
			return true;
		}
		if (wic->converting && wic->cand[0]) {
			whiz_conv_learn(wic->conversion);
			commit_utf8(ic, wic, wic->cand);
			reset_ic(ic, wic);
			return true;
		}
		if (wic->preedit_len > 0) {
			whiz_romaji_convert(wic->romaji, wic->preedit,
			                    sizeof(wic->preedit), true);
			commit_utf8(ic, wic, wic->preedit);
			reset_ic(ic, wic);
			return true;
		}
		return false;
	}

	/* --- 変換中: 上下で候補、左右で文節移動 --- */
	if (wic->converting) {
		if (sym == XKB_KEY_Up || sym == XKB_KEY_Down ||
		    sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab) {
			bool back = (sym == XKB_KEY_Up || sym == XKB_KEY_ISO_Left_Tab ||
			             (kev->state & XCB_MOD_MASK_SHIFT));
			whiz_conv_select_candidate(wic->conversion, back ? -1 : 1);
			whiz_conv_compose(wic->conversion, wic->cand, sizeof(wic->cand));
			preedit_update(ic, wic, wic->cand);
			return true;
		}
		if (sym == XKB_KEY_Left || sym == XKB_KEY_Right) {
			int d = sym == XKB_KEY_Left ? -1 : 1;
			if (kev->state & XCB_MOD_MASK_SHIFT)
				whiz_conv_resize(wic->conversion, d);
			else
				whiz_conv_select_segment(wic->conversion, d);
			whiz_conv_compose(wic->conversion, wic->cand, sizeof(wic->cand));
			preedit_update(ic, wic, wic->cand);
			return true;
		}
	}

	/* --- Space / 変換 --- */
	if (sym == XKB_KEY_space || sym == XKB_KEY_Henkan) {
		if (wic->pred_sel >= 0 && wic->n_pred > 0 && !wic->converting) {
			commit_utf8(ic, wic, wic->pred_words[wic->pred_sel]);
			reset_ic(ic, wic);
			return true;
		}
		if (wic->preedit_len > 0 && !wic->converting) {
			/* ローマ字を確定させてから変換 */
			whiz_romaji_convert(wic->romaji, wic->preedit,
			                    sizeof(wic->preedit), true);
			wic->preedit_len = (int)strlen(wic->preedit);
			wic->n_pred   = 0;
			wic->pred_sel = -1;
			if (whiz_conv_begin(wic->conversion, wic->preedit)) {
				wic->converting = 1;
				whiz_conv_compose(wic->conversion, wic->cand, sizeof(wic->cand));
				preedit_update(ic, wic, wic->cand);
			} else {
				commit_utf8(ic, wic, wic->preedit);
				reset_ic(ic, wic);
			}
			return true;
		}
		if (wic->converting) {
			whiz_conv_select_candidate(wic->conversion, 1);
			whiz_conv_compose(wic->conversion, wic->cand, sizeof(wic->cand));
			preedit_update(ic, wic, wic->cand);
			return true;
		}
		/* 読みなし → 全角スペース */
		commit_utf8(ic, wic, "\xe3\x80\x80");
		return true;
	}

	/* --- Tab: 予測候補選択 --- */
	if (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab) {
		if (wic->n_pred == 0 && wic->preedit_len > 0 && !wic->converting)
			do_predict(wic);
		if (wic->n_pred > 0 && !wic->converting) {
			bool back = (sym == XKB_KEY_ISO_Left_Tab ||
			             (kev->state & XCB_MOD_MASK_SHIFT));
			if (wic->pred_sel < 0)
				wic->pred_sel = back ? wic->n_pred - 1 : 0;
			else
				wic->pred_sel = (wic->pred_sel + (back ? -1 : 1) + wic->n_pred)
				                % wic->n_pred;
			preedit_update(ic, wic, wic->pred_words[wic->pred_sel]);
			return true;
		}
		if (wic->preedit_len > 0) {
			/* 予測なし → 変換 */
			whiz_romaji_convert(wic->romaji, wic->preedit,
			                    sizeof(wic->preedit), true);
			wic->preedit_len = (int)strlen(wic->preedit);
			if (whiz_conv_begin(wic->conversion, wic->preedit)) {
				wic->converting = 1;
				whiz_conv_compose(wic->conversion, wic->cand, sizeof(wic->cand));
				preedit_update(ic, wic, wic->cand);
			} else {
				commit_utf8(ic, wic, wic->preedit);
				reset_ic(ic, wic);
			}
			return true;
		}
		return false;
	}

	/* --- 変換中に文字キー → 候補を確定してから追加 --- */
	if (wic->converting) {
			whiz_conv_learn(wic->conversion);
			commit_utf8(ic, wic, wic->cand);
		reset_ic(ic, wic);
	}

	/* 修飾キーや制御キーはスルー */
	if (kev->state & (XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1)) return false;

	/* 通常の文字 → ローマ字バッファへ追加 */
	char utf8[8] = {0};
	int ulen = xkb_state_key_get_utf8(g_xkb_state, kev->detail, utf8, sizeof(utf8));
	if (ulen <= 0 || (unsigned char)utf8[0] < 0x20) return false;

	/* ASCII 文字のみローマ字変換対象 */
	if ((unsigned char)utf8[0] >= 0x80) {
		/* 非 ASCII は直接コミット (全角入力等) */
		commit_utf8(ic, wic, utf8);
		return true;
	}

	int rlen = (int)strlen(wic->romaji);
	if (rlen + ulen < (int)sizeof(wic->romaji) - 1) {
		memcpy(wic->romaji + rlen, utf8, (size_t)ulen + 1);
		whiz_romaji_convert(wic->romaji, wic->preedit,
		                    sizeof(wic->preedit), false);
		wic->preedit_len = (int)strlen(wic->preedit);
	}
	do_predict(wic);
	preedit_update(ic, wic, wic->preedit);
	return true;
}

// -------------------------------------------------------
// IC 解放ヘルパー
// -------------------------------------------------------
static void whiz_ic_free(void *p)
{
	WhizIC *wic = p;
	if (wic) { free(wic->conversion); free(wic); }
}

// -------------------------------------------------------
// XIM コールバック
// -------------------------------------------------------
static void xim_callback(xcb_im_t *im, xcb_im_client_t *client,
                          xcb_im_input_context_t *ic,
                          const xcb_im_packet_header_fr_t *hdr,
                          void *frame, void *arg, void *user_data)
{
	(void)im; (void)client; (void)user_data;

	switch (hdr->major_opcode) {
	case XCB_XIM_CREATE_IC: {
		WhizIC *wic = calloc(1, sizeof(*wic));
		if (!wic) break;
		wic->conversion = calloc(1, sizeof(*wic->conversion));
		if (!wic->conversion) { free(wic); break; }
		whiz_conv_clear(wic->conversion);
		wic->pred_sel = -1;
		wic->ja_mode  = getenv("WHIZ_JA") ? 1 : 0;
		xcb_im_input_context_set_data(ic, wic, whiz_ic_free);
		break;
	}
	case XCB_XIM_DESTROY_IC:
		/* set_data の free 関数が自動的に呼ばれる */
		break;

	case XCB_XIM_FORWARD_EVENT: {
		WhizIC *wic = xcb_im_input_context_get_data(ic);
		if (!wic) break;
		xcb_key_press_event_t *kev = (xcb_key_press_event_t *)arg;
		if (!kev) break;
		/* KEY_RELEASE は無視 (type の下位 7bit が 3 = KeyRelease) */
		if ((kev->response_type & 0x7f) == XCB_KEY_RELEASE) {
			xcb_im_forward_event(g_im, ic, kev);
			break;
		}
		if (!handle_key(ic, wic, kev))
			xcb_im_forward_event(g_im, ic, kev);
		break;
	}
	default:
		break;
	}
	(void)frame;
}

// -------------------------------------------------------
// main
// -------------------------------------------------------
int main(void)
{
	signal(SIGINT,  on_signal);
	signal(SIGTERM, on_signal);

	/* XCB 接続 */
	int screen_nr = 0;
	g_conn = xcb_connect(NULL, &screen_nr);
	if (!g_conn || xcb_connection_has_error(g_conn)) {
		fprintf(stderr, "whiz-xim: cannot connect to X display\n");
		return 1;
	}

	const xcb_setup_t    *setup  = xcb_get_setup(g_conn);
	xcb_screen_iterator_t iter   = xcb_setup_roots_iterator(setup);
	for (int i = 0; i < screen_nr; i++) xcb_screen_next(&iter);
	xcb_screen_t *screen = iter.data;

	/* サーバーウィンドウ (入力受付用、非表示) */
	g_win = xcb_generate_id(g_conn);
	xcb_create_window(g_conn, XCB_COPY_FROM_PARENT, g_win, screen->root,
	                  0, 0, 1, 1, 0,
	                  XCB_WINDOW_CLASS_INPUT_ONLY,
	                  screen->root_visual, 0, NULL);
	xcb_flush(g_conn);

	/* xkbcommon-x11 初期化 */
	g_xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	uint8_t xkb_ev_base, xkb_err_base;
	uint16_t xkb_maj = XKB_X11_MIN_MAJOR_XKB_VERSION;
	uint16_t xkb_min = XKB_X11_MIN_MINOR_XKB_VERSION;
	if (xkb_x11_setup_xkb_extension(g_conn, xkb_maj, xkb_min,
	                                  XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
	                                  NULL, NULL, &xkb_ev_base, &xkb_err_base) == 0) {
		fprintf(stderr, "whiz-xim: xkb extension not available\n");
	}
	int32_t device_id = xkb_x11_get_core_keyboard_device_id(g_conn);
	g_xkb_keymap = xkb_x11_keymap_new_from_device(g_xkb_ctx, g_conn,
	                                               device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
	g_xkb_state  = xkb_x11_state_new_from_device(g_xkb_keymap, g_conn, device_id);

	/* COMPOUND_TEXT エンコーディング初期化 */
	xcb_compound_text_init();

	/* Whiz エンジン初期化 */
	engine_init();

	/* XIM スタイル: PreeditCallbacks|StatusNothing と Nothing|Nothing の両方 */
	static uint32_t styles_arr[] = {
		XCB_IM_PreeditCallbacks | XCB_IM_StatusNothing,
		XCB_IM_PreeditNothing   | XCB_IM_StatusNothing,
	};
	xcb_im_styles_t styles = {
		.nStyles = sizeof(styles_arr) / sizeof(styles_arr[0]),
		.styles  = styles_arr,
	};

	/* エンコーディング */
	static xcb_im_encoding_t enc_list[] = { "COMPOUND_TEXT" };
	xcb_im_encodings_t encs = { .nEncodings = 1, .encodings = enc_list };

	g_im = xcb_im_create(g_conn, screen_nr, g_win,
	                     "whiz",            /* XMODIFIERS=@im=whiz */
	                     XCB_IM_ALL_LOCALES,
	                     &styles,
	                     NULL, NULL,        /* trigger keys (使用しない) */
	                     &encs,
	                     0,                 /* event_mask: default KEY_PRESS */
	                     xim_callback, NULL);
	if (!g_im) {
		fprintf(stderr, "whiz-xim: xcb_im_create failed\n");
		return 1;
	}

	if (!xcb_im_open_im(g_im)) {
		fprintf(stderr, "whiz-xim: xcb_im_open_im failed "
		        "(他の whiz XIM サーバーが起動中かもしれません)\n");
		return 1;
	}

	fprintf(stderr, "whiz-xim: started (XMODIFIERS=@im=whiz)\n");
	xcb_flush(g_conn);

	/* イベントループ */
	int fd = xcb_get_file_descriptor(g_conn);
	while (g_running) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		int ret = poll(&pfd, 1, 1000);
		if (ret < 0) break;

		xcb_generic_event_t *ev;
		while ((ev = xcb_poll_for_event(g_conn)) != NULL) {
			if (!xcb_im_filter_event(g_im, ev))
				free(ev);
			else
				free(ev);
		}
		if (xcb_connection_has_error(g_conn)) break;
		xcb_flush(g_conn);
	}

	xcb_im_close_im(g_im);
	xcb_im_destroy(g_im);
	xkb_state_unref(g_xkb_state);
	xkb_keymap_unref(g_xkb_keymap);
	xkb_context_unref(g_xkb_ctx);
	xcb_destroy_window(g_conn, g_win);
	xcb_disconnect(g_conn);
	fprintf(stderr, "whiz-xim: stopped\n");
	return 0;
}
