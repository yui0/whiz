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
//#include "kana_xform.h"

// -------------------------------------------------------
// whiz ConnectNet エンジン (engine/whiz.c をインライン取込)
// -------------------------------------------------------
#define WHIZ_LIBRARY
#include "engine/whiz.c"
#include "romaji.h"

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
#define CONTEXT_ID                "gim"
#define CONTEXT_ID_ALIAS          "whiz"
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

#include "im-candidate-ui.h"

static void add_style_class(GtkWidget *widget, const gchar *name)
{
	whiz_ui_class(widget, name, TRUE);
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
	WhizCandidateView candui;
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
	gboolean mode_visible;      // あ/A HUD を一時表示中
	guint    mode_timer;
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
	GdkRectangle rect = {0};
	if (cn->client_window) {
		gdk_window_get_root_coords(cn->client_window,
		    cn->cursor_location.x, cn->cursor_location.y,
		    &rect.x, &rect.y);
	}
	GtkRequisition natural;
	gtk_widget_get_preferred_size(widget, NULL, &natural);
	GdkMonitor *monitor = gdk_display_get_monitor_at_point(
		gtk_widget_get_display(widget), rect.x, rect.y);
	int x = rect.x, y = rect.y + cn->cursor_location.height + 4;
	if (monitor) {
		GdkRectangle work;
		gdk_monitor_get_workarea(monitor, &work);
		if (y + natural.height > work.y + work.height)
			y = rect.y - natural.height - 4;
		x = CLAMP(x, work.x, MAX(work.x, work.x + work.width - natural.width));
		y = CLAMP(y, work.y, MAX(work.y, work.y + work.height - natural.height));
	}
	gtk_window_move(GTK_WINDOW(widget), x, y);
}

// -------------------------------------------------------
// モードウィンドウ ("あ" / "A") の更新
// -------------------------------------------------------
static void imcontext_update_modewin(IMContextWhiz *cn)
{
	if (!cn->mode_visible || GTK_WIDGET_VISIBLE(cn->candwin)) {
		gtk_widget_hide_all(cn->modewin);
		return;
	}
	gtk_label_set_label(GTK_LABEL(cn->modelabel), cn->ja_input_mode ? "あ" : "A");
	imcontext_move_window(cn, cn->modewin);
	gtk_widget_show_all(cn->modewin);
}

static gboolean mode_timeout(gpointer data)
{
	IMContextWhiz *cn = data;
	cn->mode_timer = 0;
	cn->mode_visible = FALSE;
	imcontext_update_modewin(cn);
	return G_SOURCE_REMOVE;
}

/* Like Kotoeri: show the mode glyph briefly when the mode changes. */
static void imcontext_flash_mode(IMContextWhiz *cn)
{
	if (cn->mode_timer) g_source_remove(cn->mode_timer);
	cn->mode_visible = TRUE;
	cn->mode_timer = g_timeout_add(1000, mode_timeout, cn);
	imcontext_update_modewin(cn);
}

// -------------------------------------------------------
// 候補ウィンドウの表示 / 非表示
// -------------------------------------------------------
static void imcontext_show_candwin(IMContextWhiz *cn)
{
	gtk_window_resize(GTK_WINDOW(cn->candwin), 1, 1);
	gtk_widget_show_all(cn->candwin);
	imcontext_move_window(cn, cn->candwin);
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
	whiz_ui_update(&cn->candui, cn->preedit, &cn->pred_words[0][0],
		sizeof(cn->pred_words[0]), cn->n_pred, cn->pred_sel, 0, 0);
	imcontext_show_candwin(cn);
}

static void candwin_update_conversion(IMContextWhiz *cn)
{
	if (!cn->converting || cn->conversion->active < 0) {
		imcontext_hide_candwin(cn);
		return;
	}
	whiz_conv_compose(cn->conversion, cn->cand, sizeof(cn->cand));
	WhizConversionSegment *segment = &cn->conversion->segments[cn->conversion->active];
	whiz_ui_update(&cn->candui, segment->reading, &segment->candidates[0][0],
		sizeof(segment->candidates[0]), segment->candidate_count, segment->selected,
		cn->conversion->active, cn->conversion->segment_count);
	imcontext_show_candwin(cn);
	emit_preedit(cn);
}

/* Mouse and numeric selection use the same commit path. */
static void candidate_activate(int index, gpointer data)
{
	IMContextWhiz *cn = data;
	if (cn->converting && cn->conversion->active >= 0) {
		WhizConversionSegment *segment = &cn->conversion->segments[cn->conversion->active];
		if (index < 0 || index >= segment->candidate_count) return;
		whiz_conv_select_candidate(cn->conversion, index - segment->selected);
		whiz_conv_compose(cn->conversion, cn->cand, sizeof(cn->cand));
			whiz_conv_learn(cn->conversion);
			g_signal_emit_by_name(cn, "commit", cn->cand);
	} else {
		if (index < 0 || index >= cn->n_pred) return;
		g_signal_emit_by_name(cn, "commit", cn->pred_words[index]);
	}
	reset_preedit(cn);
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

static gboolean get_selected_text(IMContextWhiz *cn, gchar **selected,
	                              gint *offset, gint *n_chars)
{
	*selected = NULL;
	*offset = 0;
	*n_chars = 0;
	if (!cn->client_window) return FALSE;

	gpointer owner = NULL;
	gdk_window_get_user_data(cn->client_window, &owner);
	if (!owner || !GTK_IS_WIDGET(owner)) return FALSE;
	GtkWidget *widget = GTK_WIDGET(owner);
	if (GTK_IS_ENTRY(widget) && !gtk_entry_get_visibility(GTK_ENTRY(widget)))
		return FALSE;

	/* GTK 3's generic surrounding-text API has no selection anchor. Query
	 * the standard editable widgets directly and still ask the client to
	 * perform deletion through the IM-context signal below. */
	if (GTK_IS_EDITABLE(widget)) {
		gint start = 0, end = 0;
		if (!gtk_editable_get_selection_bounds(
				GTK_EDITABLE(widget), &start, &end) || start == end)
			return FALSE;
		if (start > end) { gint swap = start; start = end; end = swap; }
		*selected = gtk_editable_get_chars(GTK_EDITABLE(widget), start, end);
		*offset = start - gtk_editable_get_position(GTK_EDITABLE(widget));
		*n_chars = end - start;
		return *selected && **selected != '\0';
	}
	if (GTK_IS_TEXT_VIEW(widget)) {
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
		GtkTextIter start, end, cursor_iter;
		if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) return FALSE;
		gtk_text_buffer_get_iter_at_mark(
			buffer, &cursor_iter, gtk_text_buffer_get_insert(buffer));
		*selected = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
		*offset = gtk_text_iter_get_offset(&start) - gtk_text_iter_get_offset(&cursor_iter);
		*n_chars = gtk_text_iter_get_offset(&end) - gtk_text_iter_get_offset(&start);
		return *selected && **selected != '\0';
	}
	return FALSE;
}

static gboolean begin_reconversion(IMContextWhiz *cn, GtkIMContext *context)
{
	if (!whiz_is_ready() || cn->preedit_len > 0 || cn->romaji[0] ||
	    cn->converting) return FALSE;
	gchar *selected = NULL;
	gint offset = 0, n_chars = 0;
	if (!get_selected_text(cn, &selected, &offset, &n_chars)) return FALSE;

	const char *reversed = whiz_reverse(selected);
	gchar reading[BUFFSIZE];
	g_strlcpy(reading, reversed ? reversed : "", sizeof(reading));
	if (!reading[0] || !whiz_conv_begin(cn->conversion, reading) ||
	    !gtk_im_context_delete_surrounding(context, offset, n_chars)) {
		whiz_conv_clear(cn->conversion);
		g_free(selected);
		return FALSE;
	}

	g_strlcpy(cn->preedit, reading, sizeof(cn->preedit));
	cn->preedit_len = (int)strlen(cn->preedit);
	cn->romaji[0] = '\0';
	cn->cand[0] = '\0';
	cn->converting = 1;
	cn->n_pred = 0;
	cn->pred_sel = -1;
	g_free(selected);
	candwin_update_conversion(cn);
	return TRUE;
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
					a = pango_attr_background_new(0xb300, 0xd700, 0xff00);
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
	(void)IM_CONTEXT_WHIZ(context);
	focused_context = context;
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
	if (cn->mode_timer) { g_source_remove(cn->mode_timer); cn->mode_timer = 0; }
	cn->mode_visible = FALSE;
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

/*
 * F6–F10 の字種変換。変換中の F6–F8 は選択中の文節だけを
 * 置き換える。文節を捨てて全体を確定すると、例えば
 * 「今日は|Ubuntu」の F7 で「キョウハウブンツ」になってしまう。
 */
static gboolean apply_xform_key(IMContextWhiz *cn, int mode)
{
	if (cn->converting) {
		int active = cn->conversion->active;
		if (active < 0 || active >= cn->conversion->segment_count)
			return FALSE;

		/* Latin 変換は元ローマ字全体を使うため従来どおり確定する。 */
		if (mode >= WHIZ_XFORM_FW_ASCII) {
			cn->converting = 0;
			cn->cand[0] = '\0';
			whiz_conv_clear(cn->conversion);
			imcontext_hide_candwin(cn);
		} else {
			WhizConversionSegment *segment = &cn->conversion->segments[active];
			gchar buf[WHIZ_CONV_WORD];
			if (!whiz_xform_apply(mode, segment->reading, NULL,
			                      buf, sizeof(buf)))
				return FALSE;
			whiz_conv_copy(segment->candidates[0], WHIZ_CONV_WORD, buf);
			segment->candidate_count = 1;
			segment->selected = 0;
			candwin_update_conversion(cn);
			return TRUE;
		}
	}
	if (cn->romaji[0]) {
		whiz_romaji_convert(cn->romaji, cn->preedit,
		                    sizeof(cn->preedit), true);
		cn->preedit_len = (int)strlen(cn->preedit);
	}
	if (cn->preedit_len == 0 && !cn->romaji[0]) return FALSE;

	gchar buf[BUFFSIZE];
	if (!whiz_xform_apply(mode, cn->preedit, cn->romaji, buf, sizeof(buf)))
		return FALSE;
	g_signal_emit_by_name(cn, "commit", buf);
	reset_preedit(cn);
	return TRUE;
}

// -------------------------------------------------------
// キーイベント処理 (メイン)
// -------------------------------------------------------
static gboolean imcontext_filter_keypress(GtkIMContext *context, GdkEventKey *key)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(context);

	if (key->type == GDK_KEY_RELEASE) return FALSE;

	/* The dedicated conversion keys select a mode; they are not toggles. */
	if (key->keyval == GDK_KEY_Henkan && !cn->ja_input_mode) {
		cn->ja_input_mode = TRUE;
		g_object_set_data(G_OBJECT(context), "immodule-needs-mnemonic",
		                  (gpointer)(intptr_t)TRUE);
		if (begin_reconversion(cn, context)) return TRUE;
		imcontext_flash_mode(cn);
		return TRUE;
	}
	if (key->keyval == GDK_KEY_Muhenkan && !cn->ja_input_mode)
		return TRUE;

	// --- モード切替 ---
	if (imcontext_is_modechangekey(context, key)) {
		cn->ja_input_mode = !cn->ja_input_mode;
		g_object_set_data(G_OBJECT(context), "immodule-needs-mnemonic",
		                  (gpointer)(intptr_t)cn->ja_input_mode);
		if (!cn->ja_input_mode) {
			if (cn->converting) {
				whiz_conv_compose(cn->conversion, cn->cand, sizeof(cn->cand));
			whiz_conv_learn(cn->conversion);
			g_signal_emit_by_name(cn, "commit", cn->cand);
			} else if (cn->preedit_len > 0) {
				whiz_romaji_convert(cn->romaji, cn->preedit,
				                    sizeof(cn->preedit), true);
				g_signal_emit_by_name(cn, "commit", cn->preedit);
			}
			reset_preedit(cn);
		}
		imcontext_flash_mode(cn);
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

	// [F6–F10] → ひらがな / カタカナ / ｶﾀｶﾅ / 全角英数 / 半角英数
	if (key->keyval >= GDK_KEY_F6 && key->keyval <= GDK_KEY_F10)
		return apply_xform_key(cn, (int)(key->keyval - GDK_KEY_F6) + WHIZ_XFORM_HIRAGANA);

	// [ひらがな/カタカナ] → ひらがな、Shift 付きでカタカナ
	if (key->keyval == GDK_KEY_Hiragana_Katakana)
		return apply_xform_key(cn, (key->state & GDK_SHIFT_MASK)
			? WHIZ_XFORM_KATAKANA : WHIZ_XFORM_HIRAGANA);

	// [無変換] → 選択文節（または入力中の読み）をひらがなにする
	if (key->keyval == GDK_KEY_Muhenkan) {
		if (cn->converting)
			return apply_xform_key(cn, WHIZ_XFORM_HIRAGANA);
		if (cn->preedit_len > 0 || cn->romaji[0])
			return apply_xform_key(cn, WHIZ_XFORM_HIRAGANA);
		cn->ja_input_mode = FALSE;
		g_object_set_data(G_OBJECT(context), "immodule-needs-mnemonic", NULL);
		imcontext_flash_mode(cn);
		return TRUE;
	}

	// [ESC] → 変換/予測を解除。未変換の読みは消さない
	if (key->keyval == GDK_KEY_Escape) {
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
		return FALSE;
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

	/* Delete edits the client text.  Never commit a conversion as a side effect. */
	if (key->keyval == GDK_KEY_Delete || key->keyval == GDK_KEY_KP_Delete)
		return FALSE;

	/* Numeric shortcuts apply only to the visible conversion page. */
	gunichar digit = gdk_keyval_to_unicode(key->keyval);
	if (!(key->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) &&
	    digit >= '0' && digit <= '9') {
		if (cn->converting && cn->conversion->active >= 0) {
			WhizConversionSegment *segment = &cn->conversion->segments[cn->conversion->active];
			int index = cn->candui.first + (digit == '0' ? 9 : digit - '1');
			if (index < segment->candidate_count) {
				candidate_activate(index, cn);
				return TRUE;
			}
		}
		if (cn->converting && cn->cand[0]) {
			whiz_conv_learn(cn->conversion);
			g_signal_emit_by_name(cn, "commit", cn->cand);
			reset_preedit(cn);
		} else if (cn->preedit_len > 0 || cn->romaji[0]) {
			whiz_romaji_convert(cn->romaji, cn->preedit,
			                    sizeof(cn->preedit), true);
			g_signal_emit_by_name(cn, "commit", cn->preedit);
			reset_preedit(cn);
		}
		gchar ubuf[2] = { (gchar)digit, '\0' };
		g_signal_emit_by_name(cn, "commit", ubuf);
		return TRUE;
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
			whiz_conv_learn(cn->conversion);
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

	/* Page keys stop at the first/last candidate, including a short last page. */
	if (key->keyval == GDK_KEY_Page_Up || key->keyval == GDK_KEY_Page_Down) {
		int delta = key->keyval == GDK_KEY_Page_Up ? -WHIZ_PAGE_SIZE : WHIZ_PAGE_SIZE;
		if (cn->converting && cn->conversion->active >= 0) {
			WhizConversionSegment *s = &cn->conversion->segments[cn->conversion->active];
			int target = CLAMP(s->selected + delta, 0, s->candidate_count - 1);
			whiz_conv_select_candidate(cn->conversion, target - s->selected);
			candwin_update_conversion(cn);
			return TRUE;
		}
		if (cn->n_pred > 0) {
			cn->pred_sel = CLAMP(MAX(cn->pred_sel, 0) + delta, 0, cn->n_pred - 1);
			candwin_update_predict(cn);
			emit_preedit(cn);
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
		if (key->keyval == GDK_KEY_Henkan && begin_reconversion(cn, context))
			return TRUE;
		// 読みなし: Space だけ全角スペース。変換キーは消費する。
		if (key->keyval == GDK_KEY_space) {
			g_signal_emit_by_name(cn, "commit", "\xe3\x80\x80");
			return TRUE;
		}
		return TRUE;
	}

	// Arrow keys and Tab select predictions without moving the client cursor.
	if (key->keyval == GDK_KEY_Tab || key->keyval == GDK_KEY_ISO_Left_Tab ||
	    key->keyval == GDK_KEY_Up || key->keyval == GDK_KEY_Down) {
		if (cn->n_pred == 0 && cn->preedit_len > 0 && !cn->converting)
			do_predict(cn);
		if (cn->n_pred > 0 && !cn->converting) {
			int backwards = key->keyval == GDK_KEY_Up || key->keyval == GDK_KEY_ISO_Left_Tab
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
			whiz_conv_learn(cn->conversion);
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
	if (cn->client_window == client_window) return;
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
	cn->client_window         = NULL;
	cn->focus_in_candwin_show = FALSE;
	cn->ja_input_mode         = FALSE;
	cn->mode_visible          = FALSE;
	cn->mode_timer            = 0;

	// 候補ウィンドウ
	cn->candwin = gtk_window_new(GTK_WINDOW_POPUP);
	if (!cn->candwin) { g_critical("Failed to create candidate window"); return; }
	add_style_class(cn->candwin, "whiz-popup");
	whiz_ui_init(&cn->candui, candidate_activate, cn);
	gtk_container_add(GTK_CONTAINER(cn->candwin), cn->candui.root);
	gtk_window_resize(GTK_WINDOW(cn->candwin), 1, 1);
	gtk_window_set_accept_focus(GTK_WINDOW(cn->candwin), FALSE);

	cn->layout = gtk_widget_create_pango_layout(cn->candwin, "");
	if (!cn->layout) { g_critical("Failed to create Pango layout"); return; }

	// モードウィンドウ
	cn->modewin = gtk_window_new(GTK_WINDOW_POPUP);
	if (!cn->modewin) { g_critical("Failed to create mode window"); return; }
	add_style_class(cn->modewin, "whiz-popup");
	add_style_class(cn->modewin, "whiz-hud");
	cn->modelabel = gtk_label_new("");
	add_style_class(cn->modelabel, "whiz-mode");
	gtk_container_add(GTK_CONTAINER(cn->modewin), cn->modelabel);

	// ConnectNet エンジンを初期化 (初回のみ)
	engine_init();
}

static void imcontext_finalize(GObject *obj)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(obj);

	if (cn->mode_timer) g_source_remove(cn->mode_timer);
	g_clear_pointer(&cn->conversion, g_free);
	set_ic_client_window(cn, NULL);
	gtk_widget_destroy(GTK_WIDGET(cn->candwin));
	g_clear_object(&cn->layout);
	gtk_widget_destroy(cn->modelabel);
	gtk_widget_destroy(cn->modewin);
	parent_object_class->finalize(obj);
}

static void imcontext_class_init(gpointer g_class, gpointer class_data)
{
	(void)class_data;
	GtkIMContextClass *c = GTK_IM_CONTEXT_CLASS(g_class);
	GtkIMContextClass *im  = GTK_IM_CONTEXT_CLASS(c);
	GObjectClass      *obj = G_OBJECT_CLASS(c);
	parent_object_class = g_type_class_peek_parent(c);

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

GtkIMContextInfo im_info_alias = {
	.context_id      = CONTEXT_ID_ALIAS,
	.context_name    = CONTEXT_NAME,
	.domain          = IMDOMAIN,
	.domain_dirname  = IM_LOCALEDIR,
	.default_locales = "ja:ko:th:zh",
};

GtkIMContextInfo *info_list[] = { &im_info, &im_info_alias };

void im_module_init(GTypeModule *module)
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

void im_module_exit() {}

void im_module_list(GtkIMContextInfo ***contexts, int *n_contexts)
{
	*contexts   = info_list;
	*n_contexts = G_N_ELEMENTS(info_list);
}

GtkIMContext *im_module_create(const gchar *context_id)
{
	return strcmp(context_id, CONTEXT_ID) == 0 ||
	       strcmp(context_id, CONTEXT_ID_ALIAS) == 0
	           ? GTK_IM_CONTEXT(g_object_new(type_whiz, NULL))
	           : NULL;
}
