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
#include <gio/gio.h>
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

#define GTK_CHECK_CAST            G_TYPE_CHECK_INSTANCE_CAST
#define GTK_WIDGET_VISIBLE(w)     gtk_widget_get_visible(w)
#define IMDOMAIN                  "gtk40"

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

	/* GTK4 POPUP は角丸・影をコンポジターが制御するためCSS指定は最小限に留める */
	static const gchar css[] =
		"popover.whiz-popup > contents { background-color: #faf8ff; border: none; }"
		"popover.whiz-popup > arrow    { background-color: #faf8ff; border: none; }"
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
#if GTK_CHECK_VERSION(4, 12, 0)
	gtk_css_provider_load_from_string(provider, css);
#else
	gtk_css_provider_load_from_data(provider, css, -1);
#endif
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
		GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
	g_once_init_leave(&initialized, 1);
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

	GtkWidget   *client_widget;
	GtkWidget   *modewin;
	GtkWidget   *modelabel;
	GtkWidget   *candwin;
	GtkWidget   *candlabel;
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
static GObjectClass *parent_object_class = NULL;

// -------------------------------------------------------
// ウィンドウ配置ヘルパー
// -------------------------------------------------------
static void imcontext_move_window(IMContextWhiz *cn, GtkWidget *widget)
{
	if (!cn->client_widget || !GTK_IS_POPOVER(widget)) return;

	GdkRectangle rect = cn->cursor_location;
	if (rect.width <= 0) rect.width = 1;
	if (rect.height <= 0) rect.height = 1;
	gtk_popover_set_pointing_to(GTK_POPOVER(widget), &rect);
}

// -------------------------------------------------------
// モードウィンドウ ("あ") の更新
// -------------------------------------------------------
static void imcontext_update_modewin(IMContextWhiz *cn)
{
	if (GTK_WIDGET_VISIBLE(cn->candwin)) {
		gtk_popover_popdown(GTK_POPOVER(cn->modewin));
		return;
	}
	if (!cn->ja_input_mode) {
		gtk_popover_popdown(GTK_POPOVER(cn->modewin));
		return;
	}

	const char *label = cn->converting ? "変換" : "あ";
	gtk_label_set_label(GTK_LABEL(cn->modelabel), label);

	imcontext_move_window(cn, cn->modewin);
	gtk_popover_popup(GTK_POPOVER(cn->modewin));
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

	gtk_popover_popup(GTK_POPOVER(cn->candwin));
	imcontext_update_modewin(cn);
}

static void imcontext_hide_candwin(IMContextWhiz *cn)
{
	if (cn->candwin) gtk_popover_popdown(GTK_POPOVER(cn->candwin));
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
	gtk_popover_popdown(GTK_POPOVER(cn->modewin));
}

// -------------------------------------------------------
// モード切替キー判定
// -------------------------------------------------------
static gboolean imcontext_is_modechangekey(GtkIMContext *context,
                                            guint keyval,
                                            GdkModifierType state)
{
	(void)context;
	if (keyval == GDK_KEY_Zenkaku_Hankaku) return TRUE;
	if (state & GDK_SHIFT_MASK && keyval == GDK_KEY_space) return TRUE;
	if (state & GDK_CONTROL_MASK && keyval == GDK_KEY_space) return TRUE;
	if (keyval == GDK_KEY_Kanji) return TRUE;
	if (state & GDK_ALT_MASK && keyval == GDK_KEY_Kanji) return TRUE;
	return FALSE;
}

// -------------------------------------------------------
// キーイベント処理 (メイン)
// -------------------------------------------------------
static gboolean imcontext_filter_keypress(GtkIMContext *context, GdkEvent *event)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(context);
	if (gdk_event_get_event_type(event) == GDK_KEY_RELEASE) return FALSE;
	if (gdk_event_get_event_type(event) != GDK_KEY_PRESS) return FALSE;

	const guint keyval = gdk_key_event_get_keyval(event);
	const GdkModifierType state = gdk_event_get_modifier_state(event);

	// --- モード切替 ---
	if (imcontext_is_modechangekey(context, keyval, state)) {
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
		gunichar ukey = gdk_keyval_to_unicode(keyval);
		if (ukey == 0 || state & (GDK_CONTROL_MASK | GDK_ALT_MASK) || !g_unichar_isprint(ukey))
			return FALSE;
		gchar ubuf[7] = {0};
		g_unichar_to_utf8(ukey, ubuf);
		g_signal_emit_by_name(cn, "commit", ubuf);
		return TRUE;
	}

	// --- 日本語モード ---

	// [ESC] / [無変換] → キャンセル
	if (keyval == GDK_KEY_Escape || keyval == GDK_KEY_Muhenkan) {
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
	if (keyval == GDK_KEY_BackSpace || (state & GDK_CONTROL_MASK && keyval == GDK_KEY_h)) {
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
	if (keyval == GDK_KEY_Return || (state & GDK_CONTROL_MASK && keyval == GDK_KEY_m)) {
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
		if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down ||
		    keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) {
			int backwards = keyval == GDK_KEY_Up ||
				keyval == GDK_KEY_ISO_Left_Tab ||
				(state & GDK_SHIFT_MASK);
			whiz_conv_select_candidate(cn->conversion, backwards ? -1 : 1);
			candwin_update_conversion(cn);
			return TRUE;
		}
		if (keyval == GDK_KEY_Left || keyval == GDK_KEY_Right) {
			int delta = keyval == GDK_KEY_Left ? -1 : 1;
			if (state & GDK_SHIFT_MASK)
				whiz_conv_resize(cn->conversion, delta);
			else
				whiz_conv_select_segment(cn->conversion, delta);
			candwin_update_conversion(cn);
			return TRUE;
		}
	}

	// [Space] / [変換] → 変換実行
	if (keyval == GDK_KEY_space || keyval == GDK_KEY_Henkan) {
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
	if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) {
		if (cn->n_pred == 0 && cn->preedit_len > 0 && !cn->converting)
			do_predict(cn);
		if (cn->n_pred > 0 && !cn->converting) {
			int backwards = keyval == GDK_KEY_ISO_Left_Tab
				|| (state & GDK_SHIFT_MASK);
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
	if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) return FALSE;

	// 通常の文字 → 入力列へ追加し、確定したローマ字をひらがなへ変換
	gunichar ukey = gdk_keyval_to_unicode(keyval);
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
static void set_ic_client_widget(IMContextWhiz *cn, GtkWidget *client_widget)
{
	if (cn->client_widget == client_widget) return;

	if (gtk_widget_get_parent(cn->candwin))
		gtk_widget_unparent(cn->candwin);
	if (gtk_widget_get_parent(cn->modewin))
		gtk_widget_unparent(cn->modewin);
	if (cn->client_widget) {
		g_object_remove_weak_pointer(G_OBJECT(cn->client_widget),
			(gpointer *)&cn->client_widget);
	}

	cn->client_widget = client_widget;
	if (cn->client_widget) {
		g_object_add_weak_pointer(G_OBJECT(cn->client_widget),
			(gpointer *)&cn->client_widget);
		gtk_widget_set_parent(cn->candwin, cn->client_widget);
		gtk_widget_set_parent(cn->modewin, cn->client_widget);
	}
}
static void imcontext_set_client_widget(GtkIMContext *context, GtkWidget *widget)
{
	set_ic_client_widget(IM_CONTEXT_WHIZ(context), widget);
}

// -------------------------------------------------------
// 初期化 / 終了処理
// -------------------------------------------------------
static void imcontext_init(GTypeInstance *instance, gpointer class_data)
{
	(void)class_data;
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(instance);
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
	cn->client_widget         = NULL;
	cn->focus_in_candwin_show = FALSE;
	cn->ja_input_mode         = FALSE;

	// 候補ウィンドウ
	cn->candwin = g_object_ref_sink(gtk_popover_new());
	if (!cn->candwin) { g_critical("Failed to create candidate window"); return; }
	gtk_widget_add_css_class(cn->candwin, "whiz-popup");
	cn->candlabel = gtk_label_new("");
	gtk_widget_add_css_class(cn->candlabel, "whiz-candidates");
	gtk_label_set_xalign(GTK_LABEL(cn->candlabel), 0.0f);
	gtk_label_set_yalign(GTK_LABEL(cn->candlabel), 0.0f);
	gtk_label_set_selectable(GTK_LABEL(cn->candlabel), FALSE);
	gtk_label_set_max_width_chars(GTK_LABEL(cn->candlabel), 24);
	gtk_popover_set_child(GTK_POPOVER(cn->candwin), cn->candlabel);
	gtk_popover_set_autohide(GTK_POPOVER(cn->candwin), FALSE);
	gtk_popover_set_position(GTK_POPOVER(cn->candwin), GTK_POS_BOTTOM);

	// モードウィンドウ
	cn->modewin = g_object_ref_sink(gtk_popover_new());
	if (!cn->modewin) { g_critical("Failed to create mode window"); return; }
	gtk_widget_add_css_class(cn->modewin, "whiz-popup");
	cn->modelabel = gtk_label_new("");
	gtk_widget_add_css_class(cn->modelabel, "whiz-mode");
	gtk_popover_set_child(GTK_POPOVER(cn->modewin), cn->modelabel);
	gtk_popover_set_autohide(GTK_POPOVER(cn->modewin), FALSE);
	gtk_popover_set_position(GTK_POPOVER(cn->modewin), GTK_POS_BOTTOM);

	// ConnectNet エンジンを初期化 (初回のみ)
	engine_init();
}

static void imcontext_finalize(GObject *obj)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(obj);

	g_clear_pointer(&cn->conversion, g_free);
	set_ic_client_widget(cn, NULL);
	g_clear_object(&cn->candwin);
	g_clear_object(&cn->modewin);
	parent_object_class->finalize(obj);
}

static void imcontext_class_init(gpointer g_class, gpointer class_data)
{
	(void)class_data;
	GtkIMContextClass *c = GTK_IM_CONTEXT_CLASS(g_class);
	GtkIMContextClass *im  = GTK_IM_CONTEXT_CLASS(c);
	GObjectClass      *obj = G_OBJECT_CLASS(c);
	parent_object_class = g_type_class_peek_parent(c);

	im->set_client_widget  = imcontext_set_client_widget;
	im->filter_keypress    = imcontext_filter_keypress;
	im->get_preedit_string = imcontext_get_preedit_string;
	im->set_cursor_location = imcontext_set_cursor_location;
	im->focus_in           = imcontext_focus_in;
	im->focus_out          = imcontext_focus_out;
	im->reset              = imcontext_reset;

	obj->finalize = imcontext_finalize;
}

// -------------------------------------------------------
// GTK 4 GIO IMモジュール export
// -------------------------------------------------------
const char RegisterName[]  = "IMContextWhiz";

static void imcontext_register_type(GTypeModule *module)
{
	GTypeInfo info = {
		.class_size    = sizeof(IMContextWhizClass),
		.class_init    = imcontext_class_init,
		.instance_size = sizeof(IMContextWhiz),
		.instance_init = imcontext_init,
	};
	type_whiz = g_type_module_register_type(module, GTK_TYPE_IM_CONTEXT,
	                                        RegisterName, &info, (GTypeFlags)0);
}

G_MODULE_EXPORT void g_io_module_load(GIOModule *module)
{
	g_type_module_use(G_TYPE_MODULE(module));
	imcontext_register_type(G_TYPE_MODULE(module));
	g_io_extension_point_implement(GTK_IM_MODULE_EXTENSION_POINT_NAME,
		type_whiz, CONTEXT_ID, 10);
}

G_MODULE_EXPORT void g_io_module_unload(GIOModule *module)
{
	(void)module;
}

G_MODULE_EXPORT char **g_io_module_query(void)
{
	const char *extension_points[] = {
		GTK_IM_MODULE_EXTENSION_POINT_NAME,
		NULL
	};
	return g_strdupv((char **)extension_points);
}
