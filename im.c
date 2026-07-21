/*
 * Copyright © 2026 Yuichiro Nakada / Project Whiz
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <gtk/gtkimmodule.h>
#include "romaji.h"

// -------------------------------------------------------
// whiz ConnectNet エンジン (engine/whiz.c をインライン取込)
// -------------------------------------------------------
#define WHIZ_LIBRARY
#include "engine/whiz.c"
#include "conversion.h"

// モデル・辞書のパス (環境変数 WHIZ_MODEL / WHIZ_DIC で上書き可能)
#ifndef WHIZ_MODEL_PATH
#define WHIZ_MODEL_PATH  "/usr/share/whiz/model.wcnn"
#endif
#ifndef WHIZ_DIC_PATH
#define WHIZ_DIC_PATH    "/usr/share/whiz/whiz.web.dic"
#endif

// -------------------------------------------------------
// GTK IMモジュール定義
// -------------------------------------------------------
#define PROMPT_FMT                "[[ %s ]]"
#define CONTEXT_ID                "whiz"
#define CONTEXT_NAME              "Berry Input Method (LLM)"

#define IM_CONTEXT_TYPE_WHIZ      (type_whiz)
#define IM_CONTEXT_WHIZ(obj)      (GTK_CHECK_CAST((obj), type_whiz, IMContextWhiz))
#define IM_CONTEXT_WHIZ_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS((obj), IM_CONTEXT_TYPE_WHIZ, IMContextWhizClass))

#define IM_LOCALEDIR              "/usr/share/locale"
#define BUFFSIZE                  8192

#if GTK_MAJOR_VERSION >= 3
#define GDK_WINDOW_GET_GEOMETRY(win,x,y,w,h,d) gdk_window_get_geometry(win,x,y,w,h)
#define XDISPLAY                  gdk_x11_get_default_xdisplay
#define GDK_DRAWABLE_XID(x)       (x!=NULL ? (unsigned)GDK_WINDOW_XID(x) : 0)
#define GTK_CHECK_CAST            G_TYPE_CHECK_INSTANCE_CAST
#define gtk_widget_hide_all       gtk_widget_hide
#define gdk_drawable_get_display(w) gdk_window_get_display(w)
#define gdk_drawable_get_screen(w)  gdk_window_get_screen(w)
#define gdk_drawable_get_visual(w)  gdk_window_get_visual(w)
#define GTK_WIDGET_VISIBLE(w)     gtk_widget_get_visible(w)
#define IMDOMAIN                  "gtk30"
#else
#define XDISPLAY                  GDK_DISPLAY
#define GDK_WINDOW_GET_GEOMETRY   gdk_window_get_geometry
#define IMDOMAIN                  "gtk20"
#endif

#define MASK_CONTROL  GDK_CONTROL_MASK
#define MASK_SHIFT    GDK_SHIFT_MASK
#define MASK_CTLSFT   (GDK_SHIFT_MASK | GDK_CONTROL_MASK)
#define MASK_NONE     0

/* Hallmark · component: IME candidate popover · genre: nocturne-minimal · theme: Nocturne Violet
 * states: default · selected · composing · empty · error · inactive
 * Hallmark · pre-emit critique: P5 H5 E5 S5 R5 V5 */
static void apply_whiz_theme(void)
{
	static gsize initialized = 0;
	if (!g_once_init_enter(&initialized)) return;

	/* GTK POPUP は角丸・影をコンポジターが制御するためCSS指定は最小限に留める */
	static const gchar css[] =
		"window.whiz-popup { background-color: #faf8ff; }"
		".whiz-candidates {"
		" font-family: \"Noto Sans CJK JP\", \"Noto Sans JP\", sans-serif;"
		" font-size: 14px; color: #1a1530; padding: 6px 8px;"
		"}"
		".whiz-mode {"
		" background: #ddd5f8; color: #5a4080;"
		" border-radius: 4px; padding: 2px 5px;"
		" font-family: \"Noto Sans CJK JP\", \"Noto Sans JP\", sans-serif;"
		" font-size: 11px; font-weight: 600;"
		"}";
	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
	g_once_init_leave(&initialized, 1);
}

static void add_style_class(GtkWidget *widget, const gchar *name)
{
	gtk_style_context_add_class(gtk_widget_get_style_context(widget), name);
}

// -------------------------------------------------------
// whiz エンジン初期化 (ファイルを読んで whiz_init へ渡す)
// -------------------------------------------------------
static uint8_t *read_file(const char *path, long *out_sz)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) return NULL;
	fseek(fp, 0, SEEK_END);
	long sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	uint8_t *buf = (uint8_t *)malloc((size_t)sz);
	if (!buf || (long)fread(buf, 1, (size_t)sz, fp) != sz) {
		free(buf); fclose(fp); return NULL;
	}
	fclose(fp);
	*out_sz = sz;
	return buf;
}

static void engine_init(void)
{
	if (whiz_is_ready()) return;

	const char *mpath = getenv("WHIZ_MODEL") ? getenv("WHIZ_MODEL") : WHIZ_MODEL_PATH;
	const char *dpath = getenv("WHIZ_DIC")   ? getenv("WHIZ_DIC")   : WHIZ_DIC_PATH;

	long msz = 0, dsz = 0;
	uint8_t *mbuf = read_file(mpath, &msz);
	uint8_t *dbuf = read_file(dpath, &dsz);

	if (!mbuf || !dbuf) {
		g_warning("Whiz: failed to load model (%s) or dic (%s)", mpath, dpath);
		free(mbuf); free(dbuf);
		return;
	}
	if (!whiz_init(mbuf, (int)msz, dbuf, (int)dsz))
		g_warning("Whiz: whiz_init failed");

	free(mbuf);
	free(dbuf);
}

// -------------------------------------------------------
// かな→漢字変換 (ConnectNet 推論)
// -------------------------------------------------------

static void parse_predict_result(const char *raw,
	char words[][256], char reads[][256], int *n, int max)
{
	*n = 0;
	if (!raw || !*raw) return;
	char buf[4096];
	strncpy(buf, raw, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	char *line = buf;
	while (*line && *n < max) {
		char *nl  = strchr(line, '\n');
		if (nl) *nl = '\0';
		char *tab = strchr(line, '\t');
		if (tab) {
			*tab = '\0';
			g_strlcpy(words[*n], line, sizeof(words[*n]));
			g_strlcpy(reads[*n], tab + 1, sizeof(reads[*n]));
			(*n)++;
		}
		line = nl ? nl + 1 : line + strlen(line);
	}
}

// -------------------------------------------------------
// IM コンテキスト構造体
// -------------------------------------------------------
typedef struct {
	GtkIMContext parent;       // この位置でないとダメ！！

	GdkWindow   *client_window;
	GtkWidget   *modewin;
	GtkWidget   *modelabel;
	GtkWidget   *candwin;
	GtkWidget   *candlabel;
	PangoLayout *layout;
	GdkRectangle cursor_location;

	// 入力中の読みバッファ (UTF-8)
	gchar  preedit[BUFFSIZE];   // 読み (ひらがな等)
	int    preedit_len;         // バイト長
	gchar  romaji[BUFFSIZE];    // 未変換文字を含む入力列

	// 変換候補
	gchar  cand[BUFFSIZE];      // 変換結果 (UTF-8)
	int    converting;          // 変換中フラグ
	WhizConversion *conversion; // 文節と文節ごとの候補

	// 予測変換
	gchar  pred_words[20][256]; // 予測候補の表記
	gchar  pred_reads[20][256]; // 予測候補の読み
	int    n_pred;              // 候補数
	int    pred_sel;            // 選択中インデックス (-1=未選択)

	gboolean ja_input_mode;
	gboolean focus_in_candwin_show;
} IMContextWhiz;

typedef struct {
	GtkIMContextClass parent_class;
} IMContextWhizClass;

GType          type_whiz       = 0;
GtkIMContext  *focused_context = NULL;

// -------------------------------------------------------
// ウィンドウ配置ヘルパー
// -------------------------------------------------------
static void imcontext_move_window(IMContextWhiz *cn, GtkWidget *widget)
{
	GdkRectangle rect = {0};
	if (cn->client_window) {
		gdk_window_get_root_coords(cn->client_window,
		    cn->cursor_location.x, cn->cursor_location.y,
		    &rect.x, &rect.y);
	}
	gtk_window_move(GTK_WINDOW(widget), rect.x, rect.y + cn->cursor_location.height + 4);
}

// -------------------------------------------------------
// モードウィンドウ ("あ") の更新
// -------------------------------------------------------
static void imcontext_update_modewin(IMContextWhiz *cn)
{
	if (GTK_WIDGET_VISIBLE(cn->candwin)) {
		gtk_widget_hide_all(cn->modewin);
		return;
	}
	if (!cn->ja_input_mode) {
		gtk_widget_hide_all(cn->modewin);
		return;
	}

	const char *label = cn->converting ? "変換" : "あ";
	gtk_label_set_label(GTK_LABEL(cn->modelabel), label);

	imcontext_move_window(cn, cn->modewin);
	gtk_widget_show_all(cn->modewin);
}

// -------------------------------------------------------
// 候補ウィンドウの表示 / 非表示
// -------------------------------------------------------
static void imcontext_show_candwin(IMContextWhiz *cn, const gchar *candstr)
{
	imcontext_move_window(cn, cn->candwin);
	gtk_label_set_text(GTK_LABEL(cn->candlabel), candstr);

	PangoAttrList *attrlist = pango_attr_list_new();
	const gchar *selected = strstr(candstr, "▶ ");
	if (selected) {
		const gchar *line_end = strchr(selected, '\n');
		guint sel_start = (guint)(selected - candstr);
		guint sel_end   = line_end ? (guint)(line_end - candstr) : (guint)strlen(candstr);
		PangoAttribute *attr = pango_attr_foreground_new(0x5F00, 0x5100, 0x7A00);
		attr->start_index = sel_start; attr->end_index = sel_end;
		pango_attr_list_insert(attrlist, attr);
		attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
		attr->start_index = sel_start; attr->end_index = sel_end;
		pango_attr_list_insert(attrlist, attr);
	}
	gtk_label_set_attributes(GTK_LABEL(cn->candlabel), attrlist);
	pango_attr_list_unref(attrlist);

	gtk_widget_show_all(cn->candwin);
	imcontext_update_modewin(cn);
}

static void imcontext_hide_candwin(IMContextWhiz *cn)
{
	if (cn->candwin) gtk_widget_hide_all(cn->candwin);
	imcontext_update_modewin(cn);
}

// -------------------------------------------------------
// preedit 文字列の送出
// -------------------------------------------------------
static void emit_preedit(IMContextWhiz *cn)
{
	g_signal_emit_by_name(cn, "preedit_changed");
}

// -------------------------------------------------------
// 読みバッファをリセットして英数モードへ戻す
// -------------------------------------------------------
static void reset_preedit(IMContextWhiz *cn)
{
	cn->preedit[0]  = '\0';
	cn->preedit_len = 0;
	cn->romaji[0]   = '\0';
	cn->cand[0]     = '\0';
	cn->converting  = 0;
	whiz_conv_clear(cn->conversion);
	cn->n_pred      = 0;
	cn->pred_sel    = -1;
	imcontext_hide_candwin(cn);
	emit_preedit(cn);
}

// -------------------------------------------------------
// 予測候補ウィンドウの再描画
// -------------------------------------------------------
static void candwin_update_predict(IMContextWhiz *cn)
{
	if (cn->n_pred == 0) { imcontext_hide_candwin(cn); return; }
	gchar buf[2048];
	int o = 0;
	for (int i = 0; i < cn->n_pred && o + 300 < (int)sizeof(buf); i++) {
		if (i > 0) buf[o++] = '\n';
		if (i == cn->pred_sel)
			o += snprintf(buf + o, sizeof(buf) - o, "▶ %s", cn->pred_words[i]);
		else
			o += snprintf(buf + o, sizeof(buf) - o, "  %s", cn->pred_words[i]);
	}
	buf[o] = '\0';
	imcontext_show_candwin(cn, buf);
}

static void candwin_update_conversion(IMContextWhiz *cn)
{
	if (!cn->converting || cn->conversion->active < 0) {
		imcontext_hide_candwin(cn);
		return;
	}
	whiz_conv_compose(cn->conversion, cn->cand, sizeof(cn->cand));
	WhizConversionSegment *segment =
		&cn->conversion->segments[cn->conversion->active];
	gchar buf[4096];
	int o = snprintf(buf, sizeof(buf), "[%d/%d]\n",
		cn->conversion->active + 1, cn->conversion->segment_count);
	for (int i = 0; i < segment->candidate_count &&
	     o + WHIZ_CONV_WORD + 16 < (int)sizeof(buf); i++) {
		o += snprintf(buf + o, sizeof(buf) - (size_t)o, "%s%s%s",
			i == segment->selected ? "▶ " : "  ",
			segment->candidates[i],
			i + 1 < segment->candidate_count ? "\n" : "");
	}
	imcontext_show_candwin(cn, buf);
	emit_preedit(cn);
}

// -------------------------------------------------------
// 予測変換を実行して候補窓を更新
// -------------------------------------------------------
static void do_predict(IMContextWhiz *cn)
{
	cn->n_pred   = 0;
	cn->pred_sel = -1;
	if (cn->preedit_len == 0 || cn->converting) return;
	if (!whiz_is_ready()) return;
	const char *raw = whiz_predict(cn->preedit, 10);
	parse_predict_result(raw, cn->pred_words, cn->pred_reads, &cn->n_pred, 20);
	candwin_update_predict(cn);
}

// -------------------------------------------------------
// 変換を実行して候補をコミット or 候補窓に表示
// -------------------------------------------------------
static void do_convert(IMContextWhiz *cn)
{
	if (cn->preedit_len == 0) return;
	whiz_romaji_convert(cn->romaji, cn->preedit, sizeof(cn->preedit), true);
	cn->preedit_len = strlen(cn->preedit);

	// 予測候補をクリア
	cn->n_pred   = 0;
	cn->pred_sel = -1;

	if (whiz_conv_begin(cn->conversion, cn->preedit)) {
		cn->converting = 1;
		candwin_update_conversion(cn);
	} else {
		// 変換失敗: そのままコミット
		g_signal_emit_by_name(cn, "commit", cn->preedit);
		reset_preedit(cn);
	}
}

// -------------------------------------------------------
// imcontext_get_preedit_string
// -------------------------------------------------------
static void imcontext_get_preedit_string(GtkIMContext *ic, gchar **str,
                                         PangoAttrList **attrs, gint *cursor_pos)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(ic);

	if (attrs)      *attrs      = pango_attr_list_new();
	if (cursor_pos) *cursor_pos = 0;

	if (!cn->converting && cn->pred_sel >= 0 && cn->pred_sel < cn->n_pred) {
		// Tabで選んだ予測候補をpreedit上でもプレビューする。
		*str = g_strdup(cn->pred_words[cn->pred_sel]);
		if (attrs && *str) {
			PangoAttribute *a = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
			a->start_index = 0; a->end_index = strlen(*str);
			pango_attr_list_insert(*attrs, a);
		}
		if (cursor_pos) *cursor_pos = g_utf8_strlen(*str, -1);
	} else if (cn->converting && cn->cand[0]) {
		whiz_conv_compose(cn->conversion, cn->cand, sizeof(cn->cand));
		*str = g_strdup(cn->cand);
		if (attrs && *str) {
			int start = 0;
			for (int i = 0; i < cn->conversion->segment_count; i++) {
				int end = start + strlen(whiz_conv_selected(
					&cn->conversion->segments[i]));
				PangoAttribute *a = pango_attr_underline_new(
					i == cn->conversion->active
						? PANGO_UNDERLINE_DOUBLE : PANGO_UNDERLINE_SINGLE);
				a->start_index = start; a->end_index = end;
				pango_attr_list_insert(*attrs, a);
				if (i == cn->conversion->active) {
					a = pango_attr_background_new(0, 0, 0);
					a->start_index = start; a->end_index = end;
					pango_attr_list_insert(*attrs, a);
					a = pango_attr_foreground_new(0xffff, 0xffff, 0xffff);
					a->start_index = start; a->end_index = end;
					pango_attr_list_insert(*attrs, a);
				}
				start = end;
			}
		}
		if (cursor_pos) *cursor_pos = g_utf8_strlen(*str, -1);
	} else if (cn->preedit_len > 0) {
		// 読みを preedit として表示
		*str = g_strdup(cn->preedit);
		if (attrs && *str) {
			PangoAttribute *a;
			a = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
			a->start_index = 0; a->end_index = cn->preedit_len;
			pango_attr_list_insert(*attrs, a);
		}
		if (cursor_pos) *cursor_pos = cn->preedit_len;
	} else {
		*str = g_strdup("");
	}
}

// -------------------------------------------------------
// imcontext_reset
// -------------------------------------------------------
static void imcontext_reset(GtkIMContext *context)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(context);

	if (cn->preedit_len > 0) {
		// 確定せずにコミット (読みをそのまま)
		whiz_romaji_convert(cn->romaji, cn->preedit,
		                    sizeof(cn->preedit), true);
		g_signal_emit_by_name(cn, "commit", cn->preedit);
	}
	reset_preedit(cn);
}

// -------------------------------------------------------
// カーソル位置更新
// -------------------------------------------------------
static void imcontext_set_cursor_location(GtkIMContext *context, GdkRectangle *area)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(context);
	cn->cursor_location = *area;
	imcontext_move_window(cn, cn->candwin);
	imcontext_move_window(cn, cn->modewin);
}

// -------------------------------------------------------
// フォーカス in / out
// -------------------------------------------------------
static void imcontext_focus_in(GtkIMContext *context)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(context);
	focused_context = context;
	imcontext_update_modewin(cn);
}

static void imcontext_focus_out(GtkIMContext *context)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(context);
	focused_context = NULL;

	if (cn->preedit_len > 0) {
		whiz_romaji_convert(cn->romaji, cn->preedit,
		                    sizeof(cn->preedit), true);
		g_signal_emit_by_name(cn, "commit", cn->preedit);
		reset_preedit(cn);
	}
	gtk_widget_hide_all(GTK_WIDGET(cn->modewin));
}

// -------------------------------------------------------
// モード切替キー判定
// -------------------------------------------------------
static gboolean imcontext_is_modechangekey(GtkIMContext *context, GdkEventKey *key)
{
	(void)context;
	if (key->keyval == GDK_KEY_Zenkaku_Hankaku)         return TRUE;
	if (key->state & GDK_SHIFT_MASK && key->keyval == GDK_KEY_space) return TRUE;
	if (key->state & GDK_CONTROL_MASK && key->keyval == GDK_KEY_space) return TRUE;
	if (key->keyval == GDK_KEY_Kanji)                   return TRUE;
	if (key->state & GDK_MOD1_MASK && key->keyval == GDK_KEY_Kanji) return TRUE;
	return FALSE;
}

// -------------------------------------------------------
// キーイベント処理 (メイン)
// -------------------------------------------------------
static gboolean imcontext_filter_keypress(GtkIMContext *context, GdkEventKey *key)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(context);

	if (key->type == GDK_KEY_RELEASE) return FALSE;

	// --- モード切替 ---
	if (imcontext_is_modechangekey(context, key)) {
		cn->ja_input_mode = !cn->ja_input_mode;
		g_object_set_data(G_OBJECT(context), "immodule-needs-mnemonic",
		                  (gpointer)(intptr_t)cn->ja_input_mode);
		if (!cn->ja_input_mode) {
			if (cn->converting) {
				whiz_conv_compose(cn->conversion, cn->cand, sizeof(cn->cand));
				g_signal_emit_by_name(cn, "commit", cn->cand);
			} else if (cn->preedit_len > 0) {
				whiz_romaji_convert(cn->romaji, cn->preedit,
				                    sizeof(cn->preedit), true);
				g_signal_emit_by_name(cn, "commit", cn->preedit);
			}
			reset_preedit(cn);
		}
		imcontext_update_modewin(cn);
		return TRUE;
	}

	// --- 英数モード ---
	if (!cn->ja_input_mode) {
		gunichar ukey = gdk_keyval_to_unicode(key->keyval);
		if (ukey == 0 || key->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK) ||
		    !g_unichar_isprint(ukey))
			return FALSE;
		gchar ubuf[7] = {0};
		g_unichar_to_utf8(ukey, ubuf);
		g_signal_emit_by_name(cn, "commit", ubuf);
		return TRUE;
	}

	// --- 日本語モード ---

	// [ESC] / [無変換] → キャンセル
	if (key->keyval == GDK_KEY_Escape || key->keyval == GDK_KEY_Muhenkan) {
		if (cn->converting) {
			cn->converting = 0;
			cn->cand[0] = '\0';
			whiz_conv_clear(cn->conversion);
			do_predict(cn);
			emit_preedit(cn);
			return TRUE;
		}
		if (cn->pred_sel >= 0 && !cn->converting) {
			cn->pred_sel = -1;
			candwin_update_predict(cn);
			emit_preedit(cn);
			return TRUE;
		}
		reset_preedit(cn);
		return TRUE;
	}

	// [BackSpace] → 一文字削除
	if (key->keyval == GDK_KEY_BackSpace || (key->state & GDK_CONTROL_MASK && key->keyval == GDK_KEY_h)) {
		if (cn->converting) {
			// 変換中: 読みに戻る
			cn->converting = 0;
			cn->cand[0] = '\0';
			imcontext_hide_candwin(cn);
			emit_preedit(cn);
			return TRUE;
		}
		if (cn->preedit_len > 0) {
			whiz_romaji_backspace_kana(cn->romaji, cn->preedit,
			                          sizeof(cn->preedit));
			cn->preedit_len = strlen(cn->preedit);
			do_predict(cn);
			emit_preedit(cn);
			return TRUE;
		}
		return FALSE;
	}

	// [Return] → 確定
	if (key->keyval == GDK_KEY_Return || (key->state & GDK_CONTROL_MASK && key->keyval == GDK_KEY_m)) {
		// 予測候補が選択されていればそれをコミット
		if (cn->pred_sel >= 0 && cn->n_pred > 0 && !cn->converting) {
			g_signal_emit_by_name(cn, "commit", cn->pred_words[cn->pred_sel]);
			reset_preedit(cn);
			return TRUE;
		}
		if (cn->converting && cn->cand[0]) {
			g_signal_emit_by_name(cn, "commit", cn->cand);
			reset_preedit(cn);
			return TRUE;
		}
		if (cn->preedit_len > 0) {
			whiz_romaji_convert(cn->romaji, cn->preedit,
			                    sizeof(cn->preedit), true);
			cn->preedit_len = strlen(cn->preedit);
			g_signal_emit_by_name(cn, "commit", cn->preedit);
			reset_preedit(cn);
			return TRUE;
		}
		return FALSE;
	}

	// 変換中: Space/上下で候補、左右で文節、Shift+左右で境界を変更。
	if (cn->converting) {
		if (key->keyval == GDK_KEY_Up || key->keyval == GDK_KEY_Down ||
		    key->keyval == GDK_KEY_Tab || key->keyval == GDK_KEY_ISO_Left_Tab) {
			int backwards = key->keyval == GDK_KEY_Up ||
				key->keyval == GDK_KEY_ISO_Left_Tab ||
				(key->state & GDK_SHIFT_MASK);
			whiz_conv_select_candidate(cn->conversion, backwards ? -1 : 1);
			candwin_update_conversion(cn);
			return TRUE;
		}
		if (key->keyval == GDK_KEY_Left || key->keyval == GDK_KEY_Right) {
			int delta = key->keyval == GDK_KEY_Left ? -1 : 1;
			if (key->state & GDK_SHIFT_MASK)
				whiz_conv_resize(cn->conversion, delta);
			else
				whiz_conv_select_segment(cn->conversion, delta);
			candwin_update_conversion(cn);
			return TRUE;
		}
	}

	// [Space] / [変換] → 変換実行
	if (key->keyval == GDK_KEY_space || key->keyval == GDK_KEY_Henkan) {
		if (cn->pred_sel >= 0 && cn->n_pred > 0 && !cn->converting) {
			g_signal_emit_by_name(cn, "commit", cn->pred_words[cn->pred_sel]);
			reset_preedit(cn);
			return TRUE;
		}
		if (cn->preedit_len > 0 && !cn->converting) {
			do_convert(cn);
			return TRUE;
		}
		if (cn->converting) {
			whiz_conv_select_candidate(cn->conversion, 1);
			candwin_update_conversion(cn);
			return TRUE;
		}
		// 読みなし→全角スペース
		g_signal_emit_by_name(cn, "commit", "\xe3\x80\x80");
		return TRUE;
	}

	// [Tab]/[Shift+Tab] → 予測候補を前後に選択。候補なしなら通常変換。
	if (key->keyval == GDK_KEY_Tab || key->keyval == GDK_KEY_ISO_Left_Tab) {
		if (cn->n_pred == 0 && cn->preedit_len > 0 && !cn->converting)
			do_predict(cn);
		if (cn->n_pred > 0 && !cn->converting) {
			int backwards = key->keyval == GDK_KEY_ISO_Left_Tab
				|| (key->state & GDK_SHIFT_MASK);
			if (cn->pred_sel < 0)
				cn->pred_sel = backwards ? cn->n_pred - 1 : 0;
			else
				cn->pred_sel = (cn->pred_sel + (backwards ? -1 : 1)
					+ cn->n_pred) % cn->n_pred;
			candwin_update_predict(cn);
			emit_preedit(cn);
			return TRUE;
		}
		if (cn->preedit_len > 0) {
			do_convert(cn);
			return TRUE;
		}
		return FALSE;
	}

	// 変換中に文字キー → 現在の候補を確定してから新しい文字を追加
	if (cn->converting) {
		g_signal_emit_by_name(cn, "commit", cn->cand);
		reset_preedit(cn);
	}

	// 修飾キーや制御キーはスルー
	if (key->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) return FALSE;

	// 通常の文字 → 入力列へ追加し、確定したローマ字をひらがなへ変換
	gunichar ukey = gdk_keyval_to_unicode(key->keyval);
	if (ukey == 0 || !g_unichar_isprint(ukey)) return FALSE;

	gchar ubuf[7] = {0};
	g_unichar_to_utf8(ukey, ubuf);
	int ublen = strlen(ubuf);
	int raw_len = strlen(cn->romaji);
	if (raw_len + ublen < (int)sizeof(cn->romaji) - 1) {
		memcpy(cn->romaji + raw_len, ubuf, ublen + 1);
		whiz_romaji_convert(cn->romaji, cn->preedit,
		                    sizeof(cn->preedit), false);
		cn->preedit_len = strlen(cn->preedit);
	}
	do_predict(cn);
	emit_preedit(cn);
	return TRUE;
}

// -------------------------------------------------------
// クライアントウィンドウ設定
// -------------------------------------------------------
static void set_ic_client_window(IMContextWhiz *cn, GdkWindow *client_window)
{
	if (!client_window) return;
	if (cn->client_window) {
		g_object_unref(cn->client_window);
		cn->client_window = NULL;
	}
	if (client_window) {
		cn->client_window = g_object_ref(client_window);
	}
}
static void imcontext_set_client_window(GtkIMContext *context, GdkWindow *w)
{
	set_ic_client_window(IM_CONTEXT_WHIZ(context), w);
}

// -------------------------------------------------------
// 初期化 / 終了処理
// -------------------------------------------------------
static void imcontext_init(GtkIMContext *im_context)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(im_context);
	apply_whiz_theme();

	cn->preedit[0]  = '\0';
	cn->preedit_len = 0;
	cn->romaji[0]   = '\0';
	cn->cand[0]     = '\0';
	cn->converting  = 0;
	cn->conversion = g_new0(WhizConversion, 1);
	whiz_conv_clear(cn->conversion);
	cn->n_pred      = 0;
	cn->pred_sel    = -1;
	cn->client_window         = NULL;
	cn->focus_in_candwin_show = FALSE;
	cn->ja_input_mode         = FALSE;

	// 候補ウィンドウ
	cn->candwin = gtk_window_new(GTK_WINDOW_POPUP);
	if (!cn->candwin) { g_critical("Failed to create candidate window"); return; }
	add_style_class(cn->candwin, "whiz-popup");
	cn->candlabel = gtk_label_new("");
	add_style_class(cn->candlabel, "whiz-candidates");
	gtk_label_set_xalign(GTK_LABEL(cn->candlabel), 0.0f);
	gtk_label_set_yalign(GTK_LABEL(cn->candlabel), 0.0f);
	gtk_label_set_selectable(GTK_LABEL(cn->candlabel), FALSE);
	gtk_label_set_max_width_chars(GTK_LABEL(cn->candlabel), 24);
	gtk_container_add(GTK_CONTAINER(cn->candwin), cn->candlabel);
	gtk_window_resize(GTK_WINDOW(cn->candwin), 1, 1);
	gtk_window_set_accept_focus(GTK_WINDOW(cn->candwin), FALSE);

	cn->layout = gtk_widget_create_pango_layout(cn->candwin, "");
	if (!cn->layout) { g_critical("Failed to create Pango layout"); return; }

	// モードウィンドウ
	cn->modewin = gtk_window_new(GTK_WINDOW_POPUP);
	if (!cn->modewin) { g_critical("Failed to create mode window"); return; }
	add_style_class(cn->modewin, "whiz-popup");
	cn->modelabel = gtk_label_new("");
	add_style_class(cn->modelabel, "whiz-mode");
	gtk_container_add(GTK_CONTAINER(cn->modewin), cn->modelabel);

	// ConnectNet エンジンを初期化 (初回のみ)
	engine_init();
}

static void imcontext_finalize(GObject *obj)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(obj);

	g_clear_pointer(&cn->conversion, g_free);
	gtk_widget_destroy(GTK_WIDGET(cn->candlabel));
	gtk_widget_destroy(GTK_WIDGET(cn->candwin));
	g_object_unref(cn->layout);
	gtk_widget_destroy(cn->modelabel);
	gtk_widget_destroy(cn->modewin);
}

static void imcontext_class_init(GtkIMContextClass *c)
{
	GtkIMContextClass *im  = GTK_IM_CONTEXT_CLASS(c);
	GObjectClass      *obj = G_OBJECT_CLASS(c);

	im->set_client_window  = imcontext_set_client_window;
	im->filter_keypress    = imcontext_filter_keypress;
	im->get_preedit_string = imcontext_get_preedit_string;
	im->set_cursor_location = imcontext_set_cursor_location;
	im->focus_in           = imcontext_focus_in;
	im->focus_out          = imcontext_focus_out;
	im->reset              = imcontext_reset;

	obj->finalize = imcontext_finalize;
}

// -------------------------------------------------------
// モジュール export
// -------------------------------------------------------
const char ContextId[]     = CONTEXT_ID;
const char ContextName[]   = CONTEXT_NAME;
const char DomainName[]    = IMDOMAIN;
const char RegisterName[]  = "IMContextWhiz";

GtkIMContextInfo im_info = {
	.context_id      = CONTEXT_ID,
	.context_name    = CONTEXT_NAME,
	.domain          = IMDOMAIN,
	.domain_dirname  = IM_LOCALEDIR,
	.default_locales = "ja:ko:th:zh",
};

GtkIMContextInfo *info_list[] = { &im_info };

void im_module_init(GTypeModule *module)
{
	GTypeInfo info = {
		.class_size    = sizeof(IMContextWhizClass),
		.class_init    = (GClassInitFunc)imcontext_class_init,
		.instance_size = sizeof(IMContextWhiz),
		.instance_init = (typeof(info.instance_init))imcontext_init,
	};
	type_whiz = g_type_module_register_type(module, GTK_TYPE_IM_CONTEXT,
	                                        RegisterName, &info, (GTypeFlags)0);
}

void im_module_exit() {}

void im_module_list(GtkIMContextInfo ***contexts, int *n_contexts)
{
	*contexts   = info_list;
	*n_contexts = G_N_ELEMENTS(info_list);
}

GtkIMContext *im_module_create(const gchar *context_id)
{
	return strcmp(context_id, ContextId) == 0
	           ? GTK_IM_CONTEXT(g_object_new(type_whiz, NULL))
	           : NULL;
}
