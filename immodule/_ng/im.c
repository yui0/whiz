//---------------------------------------------------------
//	Whiz (Japanese Input Method Engine)
//
//		©2014-2023 Yuichiro Nakada
//---------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <ctype.h> // isprint
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <gtk/gtkimmodule.h>
#include "jrkanji/jrkanji.h"

#define PROMPT					"[[ %s ]]"
#define CONTEXT_ID				"gim"
#define CONTEXT_NAME				"Berry Input Method"

#define IM_CONTEXT_TYPE_WHIZ			(type_whiz)
#define IM_CONTEXT_WHIZ(obj)			(GTK_CHECK_CAST((obj), type_whiz, IMContextWhiz))
#define IM_CONTEXT_WHIZ_GET_CLASS(obj)		(G_TYPE_INSTANCE_GET_CLASS((obj), IM_CONTEXT_TYPE_WHIZ, IMContextWhizClass))

#define IM_LOCALEDIR				"/usr/share/locale"
#define BUFFSIZE				8192

#if GTK_MAJOR_VERSION>=3
#define GDK_WINDOW_GET_GEOMETRY(win,x,y,w,h,d)	gdk_window_get_geometry(win,x,y,w,h)
#define XDISPLAY				gdk_x11_get_default_xdisplay
#define GDK_DRAWABLE_XID(x)			(x!=NULL ? (unsigned)GDK_WINDOW_XID(x) : 0)

#define GTK_CHECK_CAST				G_TYPE_CHECK_INSTANCE_CAST
#define gtk_widget_hide_all			gtk_widget_hide
#define gdk_drawable_get_display(w)		gdk_window_get_display(w)
#define gdk_drawable_get_screen(w)		gdk_window_get_screen(w)
#define gdk_drawable_get_visual(w)		gdk_window_get_visual(w)
#define GTK_WIDGET_VISIBLE(w)			gtk_widget_get_visible(w)

#define IMDOMAIN				"gtk30"
#else
#define XDISPLAY				GDK_DISPLAY
#define GDK_WINDOW_GET_GEOMETRY			gdk_window_get_geometry
#define IMDOMAIN				"gtk20"
#endif


#define MASK_CONTROL	GDK_CONTROL_MASK
#define MASK_SHIFT	GDK_SHIFT_MASK
#define MASK_CTLSFT	(GDK_SHIFT_MASK | GDK_CONTROL_MASK)
#define MASK_NONE	0

static struct _gdk2canna_keytable {
	guint mask; /* Modifier key mask */
	guint gdk_keycode; /* Gdk key symbols */
	guint canna_keycode; /* Canna Key code or raw hex code */
} gdk2canna_keytable[] = {
	{ MASK_CONTROL, GDK_KEY_c, 0x03 },
	{ MASK_CONTROL, GDK_KEY_h, 0x08 },
	{ MASK_CONTROL, GDK_KEY_u, 0x15 },
	{ MASK_CONTROL, GDK_KEY_l, 0x0c },
	{ MASK_CONTROL, GDK_KEY_m, 0x0d },
	{ MASK_CONTROL, GDK_KEY_j, 0x0a }, /* Not supported yet */
	{ MASK_CONTROL, GDK_KEY_k, 0x0b },
	{ MASK_CONTROL, GDK_KEY_d, 0x04 },
	{ MASK_CONTROL, GDK_KEY_o, 0x0f },
	{ MASK_CONTROL, GDK_KEY_i, 0x09 },
	{ MASK_NONE, GDK_KEY_Henkan, 0x0e },

	// ファンクションキー
	{ MASK_NONE, GDK_KEY_F6, CANNA_KEY_F6 },
	{ MASK_NONE, GDK_KEY_F7, CANNA_KEY_F7 },
	{ MASK_NONE, GDK_KEY_F8, CANNA_KEY_F8 },
	{ MASK_NONE, GDK_KEY_F9, CANNA_KEY_F9 },
	{ MASK_NONE, GDK_KEY_F10, CANNA_KEY_F10 },

	//
	{ MASK_NONE, GDK_KEY_Up, CANNA_KEY_Up }, /* Alias to GDK_KEY_Page_Up */
	{ MASK_NONE, GDK_KEY_Left, CANNA_KEY_Left },
	{ MASK_NONE, GDK_KEY_Down, CANNA_KEY_Down }, /* Alias to GDK_KEY_Page_Down */
	{ MASK_NONE, GDK_KEY_Right, CANNA_KEY_Right },
	{ MASK_NONE, GDK_KEY_Page_Down, CANNA_KEY_Down },
	{ MASK_NONE, GDK_KEY_Page_Up, CANNA_KEY_Up },
	{ MASK_NONE, GDK_KEY_Return, 0x0D },
	{ MASK_NONE, GDK_KEY_BackSpace, 0x08 },
	{ MASK_NONE, GDK_KEY_Home, CANNA_KEY_Home },
	{ MASK_NONE, GDK_KEY_End, CANNA_KEY_End },
	{ MASK_NONE, GDK_KEY_Escape, CANNA_KEY_Help },

	{ MASK_CONTROL, GDK_KEY_b, CANNA_KEY_Left }, /* Alias to GDK_KEY_Left */
	{ MASK_CONTROL, GDK_KEY_f, CANNA_KEY_Right }, /* Alias to GDK_KEY_Right */
	{ MASK_CONTROL, GDK_KEY_n, CANNA_KEY_Down }, /* Alias to GDK_KEY_Page_Down */
	{ MASK_CONTROL, GDK_KEY_p, CANNA_KEY_Up }, /* Alias to GDK_KEY_Page_Up */
	{ MASK_CONTROL, GDK_KEY_a, CANNA_KEY_Home }, /* Alias to GDK_KEY_Home */
	{ MASK_CONTROL, GDK_KEY_e, CANNA_KEY_End }, /* Alias to GDK_KEY_End */
	{ MASK_CONTROL, GDK_KEY_g, 0x07 },
	{ MASK_NONE, GDK_KEY_Muhenkan, 0x07 },

	{ MASK_SHIFT, GDK_KEY_Right, CANNA_KEY_Shift_Right },
	{ MASK_SHIFT, GDK_KEY_Left, CANNA_KEY_Shift_Left },
	{ MASK_SHIFT, GDK_KEY_Mode_switch, 0x10 },

	{ MASK_NONE, 0, 0 },
};

typedef struct {
	GtkIMContext parent;		// この位置でないとダメ！！

	GdkWindow* client_window;		// 入力中のウインドウ
	GtkWidget* modewin;			// ステータスウインドウ
	GtkWidget* modelabel;
	GtkWidget* candwin;			// 候補ウインドウ
	GtkWidget* candlabel;
	PangoLayout* layout;
	GdkRectangle cursor_location;	// カーソルの位置

	//gint canna_context; /* Cast from pointer - FIXME */
	long long canna_context; /* Cast from pointer - FIXME */

	gint kslength;
	gchar* workbuf;
	gchar* scommit;
	jrKanjiStatus ks;
	int cand_stat;

	gboolean ja_input_mode; /* JAPANESE input mode or not */
	gboolean focus_in_candwin_show; /* the candidate window should be up at focus in */
	gboolean function_mode; /* Word Registration or other function mode */
} IMContextWhiz;

typedef struct {
	GtkIMContextClass parent_class;
} IMContextWhizClass;

GType type_whiz = 0;
GtkIMContext* focused_context = NULL;

#include "utf8.h"
gchar* utf2euc(const gchar* str)
{
/*	GError *error = NULL;
	gchar *result;
	result = g_convert(str, -1, "EUC-JP", "UTF-8", NULL, NULL, &error);
	if (!result) {
		g_warning("Error converting text from IM to UTF-8: %s\n", error->message);
		g_error_free(error);
	}
	return result;*/
	char buf[BUFFSIZE];
	/*gchar *result =*/ from_utf8(Sanitize_for_UTF8_e_EUCJP, (uint8_t*)str, strlen(str), (uint8_t*)buf, BUFFSIZE, '\0');
	return g_strdup(buf);
}
gchar *euc2utf8(const gchar *str)
{
#if 0
	GError *error = NULL;
	gchar *result = NULL;
	gsize bytes_read = 0, bytes_written = 0;

//	char buf[8192];
//	result = to_utf8(Sanitize_for_UTF8_e_EUCJP, str, strlen(str), '\0', buf, 8192, '\0', 0);
	result = g_convert(str, -1, "UTF-8", "EUC-JP", &bytes_read, &bytes_written, &error);
	if (error) {
		/* Canna outputs ideograph codes of where EUC-JP is not assigned. */
		gchar* eucprefix = (bytes_read == 0) ? g_strdup("") : g_strndup(str, bytes_read);
		gchar* prefix = euc2utf8(eucprefix);
		/* 2 bytes skip */
		gchar* converted = euc2utf8(str + bytes_read + 2);
		if (result) g_free(result);

		/* Replace to a full-width space */
		result = g_strconcat(prefix, "\xe3\x80\x80", converted, NULL);

		g_free(prefix);
		g_free(eucprefix);
		g_free(converted);
//		result = g_strdup(buf);
	} else {
		g_warning("Error converting text from IM to UTF-8: %s\n", error->message);
		g_print("Error: bytes_read: %d\n", bytes_read);
		g_print("%02X %02X\n", str[bytes_read], str[bytes_read+1]);
		g_print("Error: bytes_written: %d\n", bytes_written);
		g_error_free(error);
	}

	return result;
#endif
	char buf[BUFFSIZE];
	/*gchar *result =*/ to_utf8(Sanitize_for_UTF8_e_EUCJP, (uint8_t*)str, strlen(str), '\0', (uint8_t*)buf, BUFFSIZE, '\0', 0);
	return g_strdup(buf);
}

gint eucpos2utf8pos(gchar* euc, gint eucpos)
{
	gchar* tmpeuc = NULL;
	gchar* utf8str = NULL;
	gint result;

	if (eucpos <= 0  || euc == NULL || *euc == '\0') return 0;

	tmpeuc = g_strndup(euc, eucpos);
	utf8str = euc2utf8(tmpeuc);

	result = utf8str ? strlen(utf8str) : 0;

	if (utf8str) g_free(utf8str);
	g_free(tmpeuc);

	return result;
}

// 情報ウィンドウを適切な位置に配置する
static void imcontext_move_window(IMContextWhiz* cn, GtkWidget* widget)
{
#if 0
	GdkWindow* toplevel_gdk = cn->client_window;
	GdkScreen* screen;
	GdkWindow* root_window;
	GtkWidget* toplevel;
	GdkRectangle rect;
	GtkRequisition requisition;
	gint y;
	gint height;

	if (!toplevel_gdk) return;

	screen = gdk_drawable_get_screen(toplevel_gdk);
	root_window = gdk_screen_get_root_window(screen);

	while (1) {
		GdkWindow* parent = gdk_window_get_parent(toplevel_gdk);
		if (parent == root_window) {
			break;
		} else {
			toplevel_gdk = parent;
		}
	}

	// GdkWindow's user data is traditionally GtkWidget pointer owns it.
	gdk_window_get_user_data(toplevel_gdk, (gpointer*)&toplevel);
	if (!toplevel) return;

	height = gdk_screen_get_height(gtk_widget_get_screen(toplevel));
	gdk_window_get_frame_extents(/*toplevel->window*/gtk_widget_get_window(toplevel), &rect);
	gtk_widget_size_request(widget, &requisition);

	if (rect.y + rect.height + requisition.height < height) {
		// 画面をはみ出ていない
		y = rect.y + rect.height;
	} else {
		y = height - requisition.height;
	}

//	gtk_window_move(GTK_WINDOW(widget), rect.x/*親ウインドウ左*/, y);
//	gtk_window_move(GTK_WINDOW(widget), rect.x/*親ウインドウ左*/, y-rect.y-cn->y);
	//gtk_window_move(GTK_WINDOW(widget), cn->x+rect.x, cn->y+rect.y);

	//gtk_window_move(GTK_WINDOW(widget), rect.x+cn->cursor_location.x, rect.y+cn->cursor_location.y);
#endif
	GdkRectangle rect;
	if (cn->client_window) gdk_window_get_root_coords(cn->client_window, cn->cursor_location.x, cn->cursor_location.y, &rect.x, &rect.y);
	gtk_window_move(GTK_WINDOW(widget), rect.x, rect.y+cn->cursor_location.height+4);
}

// 「あ」を表示
static void imcontext_update_modewin(IMContextWhiz* cn)
{
	int len = 0;
	gchar* modebuf = NULL;
	gchar* modebuf_utf8 = NULL;

	PangoAttrList* attrs;
	PangoAttribute* attr;

	/*
	   Hide when the candidate window is up.
	   The mode window should play the secondary role while the candidate
	   window plays the leading role, because candidate window has more
	   important info for user input.
	 */
	if (GTK_WIDGET_VISIBLE(cn->candwin)) {
		gtk_widget_hide_all(cn->modewin);
		return;
	}

	if (!cn->ja_input_mode) {
		gtk_widget_hide_all(cn->modewin);
		return;
	}

	len = jrKanjiControl(cn->canna_context, KC_QUERYMAXMODESTR, 0);
	modebuf = g_new0(gchar, len+1);
	jrKanjiControl(cn->canna_context, KC_QUERYMODE, modebuf); // 変換モード問い合わせ

	char buf[256];
	snprintf(buf, 256, PROMPT, modebuf);
	modebuf_utf8 = euc2utf8(buf);
//	modebuf_utf8 = euc2utf8(modebuf);
	gtk_label_set_label(GTK_LABEL(cn->modelabel), modebuf_utf8);

	/* Set Mode Window Background Color to Blue */
	attrs = gtk_label_get_attributes(GTK_LABEL(cn->modelabel));
	if (attrs) {
		pango_attr_list_unref(attrs);
	}

	attrs = pango_attr_list_new();
	attr = pango_attr_background_new(0xDB00, 0xE900, 0xFF00);
	attr->start_index = 0;
	attr->end_index = strlen(modebuf_utf8);
	pango_attr_list_insert(attrs, attr);

	gtk_label_set_attributes(GTK_LABEL(cn->modelabel), attrs);
	/* - Coloring Done - */

	imcontext_move_window(cn, cn->modewin);
	gtk_widget_show_all(cn->modewin);

	g_free(modebuf_utf8);
	g_free(modebuf);
}

// 候補を表示
static void imcontext_show_candwin(IMContextWhiz* cn, gchar* candstr)
{
	PangoAttrList* attrlist = pango_attr_list_new();
	PangoAttribute* attr;

#if 0 /* No glyph for 0x3000 in FC2 -- Looks like GTK+2 bug ? */
	while ( g_utf8_strchr(labeltext, -1, 0x3000) ) {
		gchar* tmpbuf = NULL;
		gchar* tmpbuf2 = NULL;
		gchar* cursor = g_utf8_strchr(labeltext, -1, 0x3000);

		tmpbuf = g_strndup(labeltext, cursor - labeltext);
		tmpbuf2 = g_strdup(g_utf8_offset_to_pointer(cursor, 1));
		g_free(labeltext);
		labeltext = g_strconcat(tmpbuf, "  ", tmpbuf2, NULL);
		g_free(tmpbuf);
		g_free(tmpbuf2);
	}
#endif
	// ウインドウを再作成
/*	gtk_widget_destroy(cn->candlabel);
	gtk_widget_destroy(cn->candwin);
	cn->candwin = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_widget_add_events(cn->candwin, GDK_BUTTON_PRESS_MASK);
//	g_signal_connect(cn->candwin, "scroll_event", G_CALLBACK(scroll_cb), cn);
	cn->candlabel = gtk_label_new("");
	gtk_container_add(GTK_CONTAINER(cn->candwin), cn->candlabel);*/
//	gdk_window_resize(GTK_WINDOW(cn->candwin), 10, 10);
//	gtk_widget_set_size_request(cn->candwin, 10, 10);
//	gtk_widget_hide_all(cn->candwin);

	// 候補ウインドウの移動
	imcontext_move_window(cn, cn->candwin);

	char *p = candstr; // 区切り記号 0x20→\n (候補を縦に)
	while (*p) {
		if (*p == 0x20) *p = '\n';
		p++;
	}

	gchar* labeltext = euc2utf8(candstr);
	gtk_label_set_text(GTK_LABEL(cn->candlabel), labeltext);

	// 背景色
//	attr = pango_attr_background_new(0, 0, 0);
	attr = pango_attr_background_new(0xDB00, 0xE900, 0xFF00);
	attr->start_index = eucpos2utf8pos(candstr, cn->ks.gline.revPos);
	attr->end_index = eucpos2utf8pos(candstr, cn->ks.gline.revPos + cn->ks.gline.revLen);
	pango_attr_list_insert(attrlist, attr);

	// 文字色
	attr = pango_attr_foreground_new(0xffff, 0xffff, 0xffff);
	attr->start_index = eucpos2utf8pos(candstr, cn->ks.gline.revPos);
	attr->end_index = eucpos2utf8pos(candstr, cn->ks.gline.revPos + cn->ks.gline.revLen);
	pango_attr_list_insert(attrlist, attr);

	gtk_label_set_attributes(GTK_LABEL(cn->candlabel), attrlist);
	gtk_widget_show_all(cn->candwin);

	imcontext_update_modewin(cn);

	g_free(labeltext);
}

static void imcontext_hide_candwin(IMContextWhiz* cn)
{
	if (cn->candwin) {
		gtk_widget_hide_all(cn->candwin);
	}
	imcontext_update_modewin(cn);
}

static void imcontext_update_candwin(IMContextWhiz* cn)
{
	if (cn->ks.info & KanjiModeInfo) {
		imcontext_update_modewin(cn);
	}
	if (cn->ks.info & KanjiGLineInfo) {
		//printf("GLineInfo ks.mode: %s\n", cn->ks.mode);
		//printf("GLineInfo ks.gline.line: %s\n", cn->ks.gline.line);
		//printf("GLineInfo ks.gline.length: %d\n", cn->ks.gline.length);

		if (cn->ks.gline.length == 0) {
			imcontext_hide_candwin(cn);
			imcontext_update_modewin(cn); /* Expect modewin will be up */
		} else {
			imcontext_show_candwin(cn, (gchar*)cn->ks.gline.line);
		}
	}
}

// カーソルの位置を取得
// ※ウィンドウの移動・大きさ変更でも呼ばれる
// area:カーソルの場所と大きさ(クライアントウィンドウの相対位置)
static void imcontext_set_cursor_location(GtkIMContext *context, GdkRectangle *area)
{
	IMContextWhiz* cn = IM_CONTEXT_WHIZ(context);
	//DBG("imcontext = %p, x=%d, y=%d, w=%d, h=%d", imcontext, area->x, area->y, area->width, area->height);
	cn->cursor_location = *area;

	imcontext_move_window(cn, cn->candwin);
	imcontext_move_window(cn, cn->modewin);
#if 0
	gint x, y, area_h;

	/*if (cn->finalized == TRUE) {
		IM_JA_DEBUG("*ERROR* IMContext is already finalized.\n");
		return;
	}*/

	// Bug in GTK?: width & height is sometimes (on first set_location) a huge_number
	area_h = area->height;
	if ((area->width > 1000) || (area->width < 0) ||
		(area->height > 1000) || (area->height < 0)) {
		area_h = 21;
	}
	x = area->x;
	y = area->y + area_h + 1;

//	im_ja_cursor_location_changed(cn, x, y);
//	gtk_window_move(GTK_WINDOW(cn->candwin), x+20, y); // スクリーンの左上になってしまう…
//	gtk_window_move(GTK_WINDOW(cn->modewin), x, y);
//	cn->x = x;
//	cn->y = y;
#endif
}

static void imcontext_focus_in(GtkIMContext* context)
{
	IMContextWhiz* cn = IM_CONTEXT_WHIZ(context);

	focused_context = context;
	if (cn->focus_in_candwin_show == FALSE) {
		imcontext_update_modewin(cn);
		return;
	}

	gtk_widget_show_all(cn->candwin);
	imcontext_update_modewin(cn);
}

// just change mode to kana.
static void imcontext_force_to_kana_mode(IMContextWhiz* cn)
{
	jrKanjiStatusWithValue ksv;

	cn->kslength = 0;

	ksv.ks = &(cn->ks);
	ksv.val = CANNA_MODE_HenkanMode;
	ksv.buffer = (uint8_t*)cn->workbuf;
	ksv.bytes_buffer = BUFSIZ;
	jrKanjiControl(cn->canna_context, KC_CHANGEMODE, (char*)&ksv);

	imcontext_hide_candwin(cn);
	imcontext_update_modewin(cn);
}

static void imcontext_focus_out(GtkIMContext* context)
{
	IMContextWhiz* cn = IM_CONTEXT_WHIZ(context);

	focused_context = NULL;

	if (cn->kslength) {
		gchar* eucstr = g_strndup((gchar*)cn->ks.echoStr, cn->kslength);
		gchar* utf8 = euc2utf8(eucstr);
		g_signal_emit_by_name(cn, "commit", utf8);
		g_free(utf8);
		imcontext_force_to_kana_mode(cn);
		g_signal_emit_by_name(cn, "preedit_changed");
	}

	gtk_widget_hide_all(GTK_WIDGET(cn->modewin));
}


gboolean roma2kana_canna(GtkIMContext* context, gchar newinput)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(context);
	gint nbytes;

	if (cn->kslength == 0) {
		memset(cn->workbuf, 0, /*sizeof(cn->workbuf)*/BUFSIZ);
		memset(cn->scommit, 0, /*sizeof(cn->scommit)*/BUFSIZ);
	}

	nbytes = jrKanjiString(cn->canna_context, newinput, cn->scommit, BUFSIZ, &cn->ks);

	if (cn->ks.length == -1) {
		return FALSE;
	}

	cn->kslength = cn->ks.length;

	if (nbytes > 0) { // 確定!
		gchar* euc = g_strndup(cn->scommit, nbytes);
		gchar* utf8 = euc2utf8(euc);
		g_signal_emit_by_name(cn, "commit", utf8);
		g_free(euc);
		g_free(utf8);
	}

	memset(cn->workbuf, 0, BUFSIZ);
	strncpy(cn->workbuf, (char*)cn->ks.echoStr, cn->kslength);
	g_signal_emit_by_name(cn, "preedit_changed");

	if (cn->ks.info & KanjiModeInfo) {
		imcontext_update_modewin(cn);
	}
	imcontext_update_candwin(cn);

	return TRUE;
}

static guint get_canna_keysym(guint keyval, guint state)
{
	guint i = 0;

	while ( gdk2canna_keytable[i].gdk_keycode != 0
	                && gdk2canna_keytable[i].canna_keycode != 0 ) {
		guint mask = gdk2canna_keytable[i].mask;
		if ( (state & GDK_CONTROL_MASK) == (mask & GDK_CONTROL_MASK)
		                && (state & GDK_SHIFT_MASK) == (mask & GDK_SHIFT_MASK)
		                && gdk2canna_keytable[i].gdk_keycode == keyval ) {
			return gdk2canna_keytable[i].canna_keycode;
		}

		i++;
	}

	return keyval;
}

/* Mode change key combination (Shift+Space etc) or not? */
inline static gboolean imcontext_is_modechangekey(GtkIMContext *context, GdkEventKey *key)
{
	if (key->keyval == GDK_KEY_Zenkaku_Hankaku) {
		// 「全角/半角」キー
		return TRUE;
	} else if (key->keyval == GDK_KEY_Henkan) {
		// 「変換」キー: 強制的に漢字モードに
		IMContextWhiz *cn = IM_CONTEXT_WHIZ(context);
//		if (cn->ja_input_mode == FALSE) return FALSE;

		//GtkClipboard *clipboard = gtk_clipboard_get(gdk_atom_intern("CLIPBOARD", FALSE));
		gchar *text8 = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY));
		gchar *text = utf2euc(text8);
//		printf("再変換[%s]\n", text8);
		if (text) {
			cn->ks.echoStr = (uint8_t*)text;
			jrKanjiControl(cn->canna_context, KC_EX_RECONVERT, (char*)&cn->ks);
			cn->kslength = cn->ks.length;
			g_signal_emit_by_name(cn, "preedit_changed");
		}
		g_free(text8);
		g_free(text);
		cn->ja_input_mode = FALSE; // 漢字モード強制
		return TRUE;
	} else if (key->keyval == GDK_KEY_Muhenkan) {
		// 「無変換」キー: 強制的に英数モードに
		IMContextWhiz *cn = IM_CONTEXT_WHIZ(context);
		cn->ja_input_mode = TRUE;
		return TRUE;
	} else if (key->state & GDK_SHIFT_MASK && key->keyval == GDK_KEY_space) {
		/* Kinput2 style - Shift + Space */
		return TRUE;
	} else if (key->state & GDK_CONTROL_MASK && key->keyval == GDK_KEY_space) {
		/* Chinese/Korean style - Control + Space */
		return TRUE;
	} else if (key->keyval == GDK_KEY_Kanji) {
		/* Windows Style - Alt + Kanji */
		return TRUE;
	} else if (key->state & GDK_CONTROL_MASK && key->keyval == GDK_KEY_backslash) {
		/* Egg style - Control + '\' */
		return TRUE;
	}
	/* or should be customizable with dialog */

	return FALSE;
}

// キー入力処理
gboolean imcontext_filter_keypress(GtkIMContext *context, GdkEventKey *key)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(context);
	guchar canna_code = 0;

	if (key->type == GDK_KEY_RELEASE) return FALSE;
	//g_print("IM-CANNA: keyval=0x%04X\n", key->keyval);

	// 漢字モードにする？
	if (imcontext_is_modechangekey(context, key)) {
		cn->function_mode = FALSE;
		cn->ja_input_mode = !cn->ja_input_mode;
		/* Editable widget should pass mnemonic if ja-input-mode is on */
		g_object_set_data(G_OBJECT(context), "immodule-needs-mnemonic",
		                  (gpointer)(intptr_t)cn->ja_input_mode);
		imcontext_update_modewin(cn);

		if (cn->ja_input_mode == FALSE) {
			imcontext_force_to_kana_mode(cn);
			memset(cn->workbuf, 0, BUFSIZ);
			memset(cn->scommit, 0, BUFSIZ);
			g_signal_emit_by_name(cn, "preedit_changed");
		}
		return TRUE;
	}

	/* English mode */
	if (cn->ja_input_mode == FALSE) {
		gunichar ukey = gdk_keyval_to_unicode(key->keyval);

		// 修飾キー単体、修飾キーが押されている
		// Gdk handles unknown key as 0x0000
		if (ukey==0 || key->state & (GDK_CONTROL_MASK/*CTRL*/ | GDK_MOD1_MASK/*ALT*/) || !isprint(ukey)/*DEL,BS*/) {
			// GDK_SHIFT_MASK/*SHIFT*/ // SHIFT+
			return FALSE;
		}

		// For real char keys
		gchar ubuf[7] = {0};
		g_unichar_to_utf8(ukey, ubuf);
		g_signal_emit_by_name(cn, "commit", ubuf);
		return TRUE;
	}

	/*** Japanese mode ***/
	/* Function mode */
	if (cn->function_mode) {
		canna_code = get_canna_keysym(key->keyval, key->state);

		if (canna_code != 0) {
			gint nbytes = jrKanjiString(cn->canna_context, canna_code, cn->scommit, BUFSIZ, &cn->ks);
			if (nbytes == 2) {
				gchar* euc = g_strndup(cn->scommit, nbytes);
				gchar* utf8 = euc2utf8(euc);
				g_signal_emit_by_name(cn, "commit", utf8);
				g_free(euc);
				g_free(utf8);
			}
			if (cn->ks.length != -1) {
				cn->kslength = cn->ks.length;
			}
			if (strlen(cn->scommit) == 1 && cn->scommit[0] == canna_code) {
				cn->scommit[0] = '\0';
			}
			g_signal_emit_by_name(cn, "preedit_changed");
			imcontext_update_candwin(cn);
			if (GTK_WIDGET_VISIBLE(cn->modewin)) {
				/* Currently, cn->function_mode switch depends on
				 * cn->modewin visibleness, but if future im-canna always shows
				 * cn->modewin, this code will be broken.
				 */
				cn->function_mode = FALSE;
			}
			return TRUE;
		}
		return FALSE;
	}

	/* No preedit char yet */
	if (cn->kslength == 0) { // まだ入力していない
//		gchar ubuf[7] = {0};

		// 修飾キー単体、修飾キーが押されている
		if (key->state & GDK_CONTROL_MASK) {
			/*switch (key->keyval) {
			case GDK_KEY_a:
			case GDK_KEY_b:
			case GDK_KEY_f:
			case GDK_KEY_e:
			case GDK_KEY_n:
			case GDK_KEY_p:
			case GDK_KEY_o:*/
				return FALSE;
			//}
		}

		switch (key->keyval) {
		case GDK_KEY_space:
			g_signal_emit_by_name(cn, "commit", " ");
			return TRUE;

		case GDK_KEY_Return:
		case GDK_KEY_BackSpace:
		case GDK_KEY_Left:
		case GDK_KEY_Up:
		case GDK_KEY_Right:
		case GDK_KEY_Down:
		case GDK_KEY_Page_Up:
		case GDK_KEY_Page_Down:
		case GDK_KEY_End:
		case GDK_KEY_Insert:
		case GDK_KEY_Delete:
		case GDK_KEY_Shift_L:
		case GDK_KEY_Shift_R:
			return FALSE; /* Functions key handling depends on afterward codes */

		case GDK_KEY_Home:
			cn->function_mode = TRUE;
		}
	}

	canna_code = 0;
	switch (key->keyval) {
	/* All unused key in jp106 keyboard is listed up below */
	case GDK_KEY_Shift_L:
	case GDK_KEY_Shift_R:
	case GDK_KEY_F1:
	case GDK_KEY_F2:
	case GDK_KEY_F3:
	case GDK_KEY_F4:
	case GDK_KEY_F5:
	/*case GDK_KEY_F6:
	case GDK_KEY_F7:
	case GDK_KEY_F8:
	case GDK_KEY_F9:
	case GDK_KEY_F10:*/
	case GDK_KEY_F11:
	case GDK_KEY_F12:
	case GDK_KEY_Scroll_Lock:
	case GDK_KEY_Pause:
	case GDK_KEY_Num_Lock:
	case GDK_KEY_Eisu_toggle:
	case GDK_KEY_Control_L:
	case GDK_KEY_Control_R:
	case GDK_KEY_Alt_L:
	case GDK_KEY_Alt_R:
	case GDK_KEY_Hiragana_Katakana:
	case GDK_KEY_Delete:
	case GDK_KEY_Insert:
	case GDK_KEY_Home:
	case GDK_KEY_KP_Home:
	case GDK_KEY_KP_Divide:
	case GDK_KEY_KP_Multiply:
	case GDK_KEY_KP_Subtract:
	case GDK_KEY_KP_Add:
	case GDK_KEY_KP_Insert:
	case GDK_KEY_KP_Delete:
	case GDK_KEY_KP_Enter:
	case GDK_KEY_KP_Left:
	case GDK_KEY_KP_Up:
	case GDK_KEY_KP_Right:
	case GDK_KEY_KP_Down:
	case GDK_KEY_KP_Begin:
	case GDK_KEY_KP_Page_Up:
	case GDK_KEY_KP_Page_Down:
	//case GDK_KEY_Escape:
	case 0x0000: /* Unknown(Unassigned) key */
		return FALSE;

	case GDK_KEY_Return:
		if (!GTK_WIDGET_VISIBLE(cn->candwin)) {
			/*
			 * Disable Furigana support code because it doesn't make sense
			 * when kakutei-if-end-of-bunsetsu is set as 't' in ~/.canna
			 *
			 */
#if 0
			imcontext_get_furigana(cn);
#endif
		}
		break;

	default:
		canna_code = get_canna_keysym(key->keyval, key->state);
	}

	/* Tab to commit and focus next editable widget.
	 *  This code can be removed if Mozilla turns to call
	 *  gtk_im_context_focus_out() when focus moved from widget
	 *  to widget.
	 *
	 *  But this code doesn't have inconsistency with focus out
	 *  code, so it might not need to be removed at that time.
	 */
	if (key->keyval == GDK_KEY_Tab) {
		gchar* utf8 = NULL;
		if (!cn->workbuf) return FALSE; // avoid segfault
		memset(cn->workbuf, 0, BUFSIZ);
		strncpy(cn->workbuf, (char*)cn->ks.echoStr, cn->kslength);
		utf8 = euc2utf8(cn->workbuf);
		if (!utf8) return FALSE; // no need?
		g_signal_emit_by_name(cn, "commit", utf8);
		imcontext_force_to_kana_mode(cn);
		memset(cn->workbuf, 0, BUFSIZ);
		memset(cn->scommit, 0, BUFSIZ);
		cn->ks.echoStr[0] = '\0';
		cn->kslength= 0;
		g_signal_emit_by_name(cn, "preedit_changed");
		imcontext_update_candwin(cn);
		g_free(utf8);
		return FALSE;
	}

	// 入力があったか？
	if (canna_code != 0) {
		memset(cn->scommit, 0, BUFSIZ);
		jrKanjiString(cn->canna_context, canna_code, cn->scommit, BUFSIZ, &cn->ks);
		if (cn->ks.length != -1) {
			cn->kslength = cn->ks.length;
		}
		if (strlen(cn->scommit) == 1 && cn->scommit[0] == canna_code) {
			cn->scommit[0] = '\0';
		}

		// 確定したか？
		if (*cn->scommit != '\0') {
			gchar* utf8 = euc2utf8(cn->scommit);
			g_signal_emit_by_name(cn, "commit", utf8);
			memset(cn->scommit, 0, BUFSIZ);
			// まだ変換中か？
			if (cn->ks.length < 1) {
				memset(cn->workbuf, 0, BUFSIZ);
				cn->kslength = 0;
			}
			g_free(utf8);
		}

		g_signal_emit_by_name(cn, "preedit_changed");
		imcontext_update_candwin(cn);
		return TRUE;
	}

	/* Pass char to Canna, anyway */
	if (roma2kana_canna(context, key->keyval)) {
		return TRUE;
	}

	return FALSE;
}

// return utf8 index from multibyte string and multibyte index
static int index_mb2utf8(gchar* mbstr, int revPos)
{
	gchar* u8str;
	gchar* mbbuf;
	gint ret = 0;

	if (mbstr == NULL || *mbstr == '\0') return 0;
	if (revPos <= 0 || (int)strlen(mbstr) < revPos) return 0;

	mbbuf = g_strndup(mbstr, revPos);
	u8str = euc2utf8(mbbuf);
	g_free(mbbuf);

	ret = strlen(u8str);
	g_free(u8str);

	return ret;
}

// 編集中の文字列を表示する(開始時にすぐ呼ばれる)
void imcontext_get_preedit_string(GtkIMContext *ic, gchar **str, PangoAttrList **attrs, gint *cursor_pos)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(ic);
	gchar* eucstr = NULL;

	if (attrs) *attrs = pango_attr_list_new(); // ないとだめ
	if (cursor_pos) *cursor_pos = 0;

	if (cn->kslength == 0) {
		*str = g_strdup("");
		return;
	}
	if (cn->ks.echoStr == NULL || *cn->ks.echoStr == '\0') {
		*str = g_strdup("");
		return;
	}

	eucstr = g_strndup((gchar*)cn->ks.echoStr, cn->kslength);
	*str = euc2utf8(eucstr);
	g_free(eucstr);

	if (*str == NULL || strlen(*str) == 0) return;

	if (attrs) {
		PangoAttribute *attr;
		// 下線
		attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
		attr->start_index = 0;
		attr->end_index = strlen(*str);
		pango_attr_list_insert(*attrs, attr);

		// 選択時の背景色
		attr = pango_attr_background_new(0, 0, 0);
		attr->start_index = index_mb2utf8((gchar*)cn->ks.echoStr, cn->ks.revPos);
		attr->end_index = index_mb2utf8((gchar*)cn->ks.echoStr, cn->ks.revPos+cn->ks.revLen);
		pango_attr_list_insert(*attrs, attr);

		// 選択時の文字色
		attr = pango_attr_foreground_new(0xffff, 0xffff, 0xffff);
		attr->start_index = index_mb2utf8((gchar*)cn->ks.echoStr, cn->ks.revPos);
		attr->end_index = index_mb2utf8((gchar*)cn->ks.echoStr, cn->ks.revPos+cn->ks.revLen);
		pango_attr_list_insert(*attrs, attr);
	}

	if (cursor_pos) {
		// カーソル位置はバイトではなく文字単位
		*cursor_pos = strlen(*str);
	}
}

#if 0
typedef struct _Furigana {
	gchar* text; /* Furigana is in UTF-8 */
	gint foffset; /* Unused */
	gint offset; /* Offset is in UTF-8 chars, not bytes */
	gint length; /* Length is in UTF-8 chars, not bytes */
} Furigana;

static gint imcontext_get_utf8_len_from_euc(guchar *text_euc)
{
	gchar *text_utf8;
	gint utf8_len;

	text_utf8 = euc2utf8(text_euc);

	utf8_len = g_utf8_strlen(text_utf8, -1);

	g_free(text_utf8);

	return utf8_len;
}

static gint imcontext_get_utf8_pos_from_euc_pos(guchar *text_euc, gint pos)
{
	gchar *tmp_euc;
	gchar *tmp_utf8;
	gint utf8_pos;

	tmp_euc = g_strndup(text_euc, pos);
	tmp_utf8 = euc2utf8(tmp_euc);

	utf8_pos = g_utf8_strlen(tmp_utf8, -1);

	g_free (tmp_euc);
	g_free (tmp_utf8);

	return utf8_pos;
}

static gboolean imcontext_is_euc_char(guchar* p)
{
	if ((p != NULL) &&
        (*(p+0) != '\0') &&
        (*(p+1) != '\0') &&
        (*(p+0) & 0x80) &&
        (*(p+1) & 0x80)) {
		return TRUE;
	} else {
		return FALSE;
	}
}

/* Checking Hiragana in EUC-JP
 *  212b, 212c, 213c, 2421-2473 (JIS)
 *  A1AB, A1AC, A1BC, A4A1-A4F3 (EUC)
 */
static gboolean imcontext_is_euc_hiragana_char(guchar* p)
{
	if ((p == NULL)      ||
	                (*(p+0) == '\0') ||
	                (*(p+1) == '\0')) {
		return FALSE;
	} else {
		guint16 mb;

		mb = ((*(p+0) << 8) |
		      (*(p+1) << 0));

		if ((mb == 0xA1AB) ||
		                (mb == 0xA1AC) ||
		                (mb == 0xA1BC) ||
		                ((mb >= 0xA4A1) && (mb <= 0xA4F3))) {
			return TRUE;
		} else {
			return FALSE;
		}
	}
}

static gboolean imcontext_is_euc_hiragana(guchar* text)
{
	guchar *p = text;

	if ( text == NULL || *text == '\0' ) {
		return FALSE;
	}

	if (strlen(p) < 2) {
		return FALSE;
	}

	while (strlen(p) >= 2) {
		if (imcontext_is_euc_hiragana_char(p) == FALSE) {
			return FALSE;
		}

		p += 2;

		if ( strlen(p) == 1 ) {
			return FALSE;
		}

		if ( strlen(p) == 0 ) {
			return TRUE;
		}
	}

	return FALSE;
}

/* Checking Kanji in EUC-JP
 *  (rough area for faster calc, and jisx0208 only)
 *  3021-7424 (JIS)
 *  B0A1-F4A4 (EUC)
 */
/*static gboolean imcontext_is_euc_kanji(guchar* text)
{
	guchar *p = text;

	if ( text == NULL || *text == '\0' ) {
		return FALSE;
	}

	if (strlen(p) < 2) {
		return FALSE;
	}

	while (strlen(p) >= 2) {
		guint16 mb = (p[0]<<8) + p[1];

		if ( !(mb >= 0xB0A1 && mb <= 0xF4A4) ) {
			return FALSE;
		}

		p += 2;

		if ( strlen(p) == 1 ) {
			return FALSE;
		}

		if ( strlen(p) == 0 ) {
			return TRUE;
		}
	}

	return FALSE;
}*/

static gint imcontext_get_pre_hiragana_len(guchar* text)
{
	gint pre_hiragana_len;
	guchar *p;
	int text_len;
	int i;

	pre_hiragana_len = 0;

	text_len = strlen(text);

	i = 0;
	while (i < text_len) {
		if ((text_len - i) < 2) {
			break;
		}

		p = (text + i);

		if (imcontext_is_euc_hiragana_char(p) == TRUE) {
			i += 2;

			pre_hiragana_len += 2;
		} else {
			break;
		}
	}

	return pre_hiragana_len;
}

static gint imcontext_get_post_hiragana_len(guchar* text)
{
	gint post_hiragana_len;
	guchar *p;
	guchar *tmp;
	int text_len;
	int i;

	tmp = NULL;
	text_len = strlen(text);

	i = 0;
	while (i < text_len) {
		if ((text_len - i) < 2) {
			tmp = NULL;
			break;
		}

		p = (text + i);

		if (imcontext_is_euc_hiragana_char(p) == TRUE) {
			if (tmp == NULL) {
				tmp = p;
			}

			i += 2;
		} else {
			tmp = NULL;

			if (imcontext_is_euc_char(p) == TRUE) {
				i += 2;
			} else {
				i += 1;
			}
		}
	}

	if (tmp != NULL) {
		post_hiragana_len = ((text + text_len) - tmp);
	} else {
		post_hiragana_len = 0;
	}

	return post_hiragana_len;
}

/* Hash Table Functions */
void strhash_dumpfunc(gpointer key, gpointer value, gpointer user_data)
{
	printf("key: %s\n", (guchar*)key);
}
void strhash_removefunc(gpointer key, gpointer value, gpointer user_data)
{
	g_free(key);
}

static GSList *imcontext_get_furigana_slist(gchar *kakutei_text_euc, gchar *muhenkan_text_euc)
{
	GSList *furigana_slist;
	gint kakutei_text_euc_len;
	gint muhenkan_text_euc_len;
	gint pre_hiragana_len;
	gint post_hiragana_len;
	gboolean as_is;


	furigana_slist = NULL;

	pre_hiragana_len  = imcontext_get_pre_hiragana_len(kakutei_text_euc);
	post_hiragana_len = imcontext_get_post_hiragana_len(kakutei_text_euc);

	kakutei_text_euc_len  = strlen(kakutei_text_euc);
	muhenkan_text_euc_len = strlen(muhenkan_text_euc);

	as_is = FALSE;

	if ((as_is == FALSE) &&
	                ((pre_hiragana_len + post_hiragana_len) > muhenkan_text_euc_len)) {
		as_is = TRUE;
	}

	if ((as_is == FALSE) &&
	                (pre_hiragana_len > 0)) {
		if ((strncmp (kakutei_text_euc,
		                muhenkan_text_euc,
		                pre_hiragana_len) != 0)) {
			as_is = TRUE;
		}
	}

	if ((as_is == FALSE) &&
	                (post_hiragana_len > 0)) {
		if ((strncmp (((kakutei_text_euc
		                + kakutei_text_euc_len)
		                - (post_hiragana_len)),
		                ((muhenkan_text_euc
		                  + muhenkan_text_euc_len)
		                 - (post_hiragana_len)),
		                post_hiragana_len) != 0)) {
			as_is = TRUE;
		}
	}

	if (as_is == FALSE) {
		gchar *tmp_kakutei_text_euc;
		gchar *tmp_muhenkan_text_euc;
		gint   tmp_kakutei_text_euc_len;
		gint   tmp_muhenkan_text_euc_len;

		tmp_kakutei_text_euc
		        = g_strndup((kakutei_text_euc + pre_hiragana_len),
		                     (kakutei_text_euc_len
		                      - pre_hiragana_len
		                      - post_hiragana_len));
		tmp_muhenkan_text_euc
		        = g_strndup((muhenkan_text_euc + pre_hiragana_len),
		                     (muhenkan_text_euc_len
		                      - pre_hiragana_len
		                      - post_hiragana_len));

		tmp_kakutei_text_euc_len  = strlen(tmp_kakutei_text_euc);
		tmp_muhenkan_text_euc_len = strlen(tmp_muhenkan_text_euc);

		{
			struct _Hiragana {
				gint pos;
				gint len;

				gint pos2;
			};
			typedef struct _Hiragana Hiragana;

			GList *hiragana_list;
			GList *tmp;
			Hiragana *hiragana;
			int i;
			gchar *p;

			hiragana_list = NULL;
			hiragana = NULL;

			i = 0;
			while (i < tmp_kakutei_text_euc_len) {
				if ((tmp_kakutei_text_euc_len - i) < 2) {
					if (hiragana != NULL) {
						hiragana->len = (i - hiragana->pos);
						hiragana = NULL;
					}

					break;
				}

				p = (tmp_kakutei_text_euc + i);

				if (imcontext_is_euc_hiragana_char(p) == TRUE) {
					if (hiragana == NULL) {
						hiragana = g_new0(Hiragana, 1);

						hiragana->pos = i;
						hiragana_list = g_list_append(hiragana_list, hiragana);
					}

					i += 2;
				} else {
					if (hiragana != NULL) {
						hiragana->len = (i - hiragana->pos);
						hiragana = NULL;
					}

					if (imcontext_is_euc_char(p) == TRUE) {
						i += 2;
					} else {
						i += 1;
					}
				}
			}

			if (hiragana_list != NULL) {
				GList *tmp2;
				Hiragana *h;
				gint l1;
				gint l2;
				gboolean match;
				gchar *p;

				tmp = hiragana_list;

				while (tmp != NULL) {
					hiragana = (Hiragana *)tmp->data;
					tmp = g_list_next (tmp);

					l1 = 0;
					tmp2 = g_list_previous (tmp);
					while (tmp2 != NULL) {
						h = (Hiragana *)tmp2->data;
						tmp2 = g_list_previous (tmp2);

						l1 += h->len;
					}

					l2 = 0;
					tmp2 = g_list_next (tmp);
					while (tmp2 != NULL) {
						h = (Hiragana *)tmp2->data;
						tmp2 = g_list_next (tmp2);

						l2 += h->len;
					}

					match = FALSE;
					i = 0;
					while (i < (tmp_muhenkan_text_euc_len - l1 - l2 )) {
						if (((tmp_muhenkan_text_euc_len - l1 - l2) - i)
						                < (hiragana->len)) {
							if (match == FALSE) {
								as_is = TRUE;
							}
							break;
						}

						p = (tmp_muhenkan_text_euc + l1 + i);

						if (imcontext_is_euc_char(p) == TRUE) {
							if (strncmp (p,
							                (tmp_kakutei_text_euc + hiragana->pos),
							                hiragana->len)
							                == 0) {
								if (match == FALSE) {
									match = TRUE;
									hiragana->pos2 = (l1 + i);
								} else {
									as_is = TRUE;
									break;
								}
							}

							i += 2;
						} else {
							i += 1;
						}
					}

					if (match == FALSE) {
						as_is = TRUE;
						break;
					}
				}

				if (as_is == FALSE) {
					Furigana *furigana;
					gint pos;
					gint pos2;
					gchar *tmp_euc;

					pos = 0;
					pos2 = 0;

					{
						hiragana = g_new0(Hiragana, 1);

						hiragana->pos = tmp_kakutei_text_euc_len;
						hiragana->len = 0;
						hiragana->pos2 = tmp_muhenkan_text_euc_len;

						hiragana_list = g_list_append(hiragana_list, hiragana);
					}

					tmp = hiragana_list;
					while (tmp != NULL) {
						hiragana = (Hiragana *)tmp->data;
						tmp = g_list_next (tmp);

						furigana = g_new0(Furigana, 1);

						{
							gint p1;
							gint p2;

							p1 = imcontext_get_utf8_pos_from_euc_pos
							     (kakutei_text_euc,
							      (pre_hiragana_len +pos));
							p2 = imcontext_get_utf8_pos_from_euc_pos
							     (kakutei_text_euc,
							      (pre_hiragana_len + hiragana->pos));

							pos = (hiragana->pos + hiragana->len);

							furigana->offset = p1;
							furigana->length = (p2 - p1);
						}

						{
							gchar *tmp_utf8;

							tmp_euc = g_strndup((tmp_muhenkan_text_euc + pos2),
							                     (hiragana->pos2 - pos2));
							tmp_utf8 = euc2utf8(tmp_euc);
							g_free (tmp_euc);

							pos2 = (hiragana->pos2 + hiragana->len);

							furigana->text = tmp_utf8;
						}

						furigana_slist = g_slist_append(furigana_slist, furigana);
					}
				}


				g_list_foreach (hiragana_list, (GFunc)g_free, NULL);
				g_list_free (hiragana_list);
			} else {
				Furigana *furigana;
				gchar *text_utf8;
				gint length;
				gint offset;


				text_utf8 = euc2utf8(tmp_muhenkan_text_euc);
				length = imcontext_get_utf8_len_from_euc(tmp_kakutei_text_euc);
				offset = imcontext_get_utf8_pos_from_euc_pos(kakutei_text_euc, pre_hiragana_len);


				furigana = g_new0(Furigana, 1);

				furigana->text   = text_utf8;
				furigana->length = length;
				furigana->offset = offset;

				furigana_slist = g_slist_append(furigana_slist, furigana);
			}
		}

		g_free (tmp_kakutei_text_euc);
		g_free (tmp_muhenkan_text_euc);
	}

	if (as_is == TRUE) {
		Furigana *furigana;
		gchar *text_utf8;
		gchar *tmp_utf8;


		text_utf8 = euc2utf8(muhenkan_text_euc);
		tmp_utf8  = euc2utf8(kakutei_text_euc);


		furigana = g_new0(Furigana, 1);

		furigana->text   = text_utf8;
		furigana->length = g_utf8_strlen(tmp_utf8, -1);
		furigana->offset = 0;


		g_free (tmp_utf8);


		furigana_slist = g_slist_append(furigana_slist, furigana);
	}

	return furigana_slist;
}

/*
 * Strict Furigana Discard function
 * Utf8 used internally
 */
GSList* discard_strict_furigana(gchar* ptext, GSList* furigana_slist)
{
	return furigana_slist;
}

static GSList* imcontext_get_furigana(IMContextWhiz* cn)
{
	GSList* furigana_slist = NULL;

	gchar* finalstr = NULL; /* Final input string */
	gchar buffer[BUFSIZ]; /* local buffer */

	Furigana* f;
	guchar* reftext;
	guchar* candtext = NULL;
	guchar* recovertext = NULL;
	gchar* tmpbuf = NULL;

	gint   chunk_kakutei_text_euc_pos;
	gchar *chunk_kakutei_text_euc;
	gchar *chunk_muhenkan_text_euc;

	g_return_val_if_fail(cn->kslength > 0, NULL);

	chunk_kakutei_text_euc_pos = -1;
	chunk_kakutei_text_euc  = NULL;
	chunk_muhenkan_text_euc = NULL;


	finalstr = g_strndup(cn->ks.echoStr, cn->kslength);

	/* Discard Furigana for Hiragana */
	if ( imcontext_is_euc_hiragana(finalstr) ) {
		g_object_set_data(G_OBJECT(cn), "furigana", NULL);
		g_free(finalstr);
		return NULL;
	}

	memset(buffer, 0, BUFSIZ);
	strcpy(buffer, finalstr);

	static int debug_loop = 0;
	while (cn->ks.revPos != 0) {
		jrKanjiString(cn->canna_context, 0x06, buffer, BUFSIZ, &cn->ks); /* Ctrl+F */

		//    printf("pos: %d\n", cn->ks.revPos);
		//    printf("Len: %d\n", cn->ks.revLen);

		if (debug_loop++ > 100) {
			/* Happens when the candidate window */
			// printf("FURIGANA: while loop exceeded. (>100) Bug!!\n");
			g_object_set_data(G_OBJECT(cn), "furigana", NULL);
			return NULL;
		}
	}

	g_assert(cn->ks.revLen > 0);

	do {
		recovertext = NULL;
		reftext = g_strndup(cn->ks.echoStr+cn->ks.revPos, cn->ks.revLen);
		// printf("reftext: %s\n", reftext);
		if ( imcontext_is_euc_hiragana(reftext) ) {
			g_free(reftext);
			jrKanjiString(cn->canna_context, 0x06, buffer, BUFSIZ, &cn->ks); /* Ctrl+F */
			continue;
		}

		chunk_kakutei_text_euc_pos = cn->ks.revPos;
		chunk_kakutei_text_euc     = g_strdup (reftext);

		if ( !imcontext_is_euc_hiragana(reftext) ) {
			GHashTable* loop_check_ht = g_hash_table_new(g_str_hash, g_str_equal);
			candtext = g_strdup(reftext);
			recovertext = g_strdup(""); /* Something free-able but not reftext */
			/* Loop until Hiragana for Furigana shows up */
			while ( !imcontext_is_euc_hiragana(candtext) ) {
				guint strcount = 0;

				g_free(candtext);
				jrKanjiString(cn->canna_context, 0x0e, buffer, BUFSIZ, &cn->ks); /* Ctrl+N */
				candtext = g_strndup(cn->ks.echoStr+cn->ks.revPos, cn->ks.revLen);

				strcount = (guint)g_hash_table_lookup(loop_check_ht, candtext);

				if ( strcount >= 4 ) { /* specify threshold */
					// printf("Looping, No Furigana: %s\n", reftext);
					g_hash_table_foreach(loop_check_ht, strhash_dumpfunc, NULL);
					g_free(candtext);
					candtext = NULL;
					break;
				}

				g_hash_table_insert(loop_check_ht, g_strdup(candtext),
				                    (gpointer)++strcount);
			}
			g_hash_table_foreach(loop_check_ht, strhash_removefunc, NULL);
			g_hash_table_destroy(loop_check_ht);
			// printf("candtext: %s\n", candtext ? candtext : (guchar*)("NO-FURIGANA"));

			chunk_muhenkan_text_euc = g_strdup (candtext);

			loop_check_ht = g_hash_table_new(g_str_hash, g_str_equal);
			/* Loop again to recover the final kakutei text */
			while ( strcmp(recovertext, reftext) != 0 ) {
				guint strcount;

				g_free(recovertext);
				jrKanjiString(cn->canna_context, 0x0e, buffer, BUFSIZ, &cn->ks); /* Ctrl+N */
				recovertext = g_strndup(cn->ks.echoStr+cn->ks.revPos, cn->ks.revLen);

				strcount = (guint)g_hash_table_lookup(loop_check_ht, recovertext);
				if ( strcount >= 4 ) { /* threshold */
					/* Loop detected - Give up recovery */
					jrKanjiString(cn->canna_context, 0x07, buffer, BUFSIZ, &cn->ks); /* Ctrl+G */
					printf("Cannot recover: %s\n", reftext);
					g_hash_table_foreach(loop_check_ht, strhash_dumpfunc, NULL);
					if ( candtext ) {
						g_free(candtext);
					}
					candtext = NULL;
					break;
				}
				g_hash_table_insert(loop_check_ht, g_strdup(recovertext),
				                    (gpointer)++strcount);
			}
			g_hash_table_foreach(loop_check_ht, strhash_removefunc, NULL);
			g_hash_table_destroy(loop_check_ht);

			if ( recovertext != NULL ) {
				g_free(recovertext);
			}
			if ( reftext != NULL ) {
				g_free(reftext);
			}
			reftext = candtext;
		}
		if ((chunk_muhenkan_text_euc != NULL) &&
		                (*chunk_muhenkan_text_euc != '\0')) {
			gint offset0;
			GSList *tmp_slist;
			GSList *tmp;
			Furigana *furigana;

			offset0 = imcontext_get_utf8_pos_from_euc_pos
			          (finalstr, chunk_kakutei_text_euc_pos);

			tmp_slist = imcontext_get_furigana_slist (chunk_kakutei_text_euc,
			                chunk_muhenkan_text_euc);

			tmp = tmp_slist;
			while (tmp != NULL) {
				furigana = (Furigana *)tmp->data;
				tmp = g_slist_next (tmp);

				furigana->offset += offset0;
			}

			furigana_slist = g_slist_concat (furigana_slist, tmp_slist);
		}

		if (chunk_kakutei_text_euc != NULL) {
			g_free (chunk_kakutei_text_euc);
		}
		if (chunk_muhenkan_text_euc != NULL) {
			g_free (chunk_muhenkan_text_euc);
		}

		jrKanjiString(cn->canna_context, 0x06, buffer, BUFSIZ, &cn->ks); /* Ctrl+F */
	} while ( cn->ks.revPos > 0 && cn->ks.revLen > 0 );

	furigana_slist = discard_strict_furigana(finalstr, furigana_slist);
	g_free(finalstr);

	if (0) { /* Dump furigana slist */
		int i = 0;
		for (i=0; i<g_slist_length(furigana_slist); i++) {
			Furigana* f = g_slist_nth_data(furigana_slist, i);
			g_print("IM_CANNA: furigana[%d]=\"%s\", pos=%d, len=%d\n",
			        i, f->text, f->offset, f->length);
		}
	}

	g_object_set_data(G_OBJECT(cn), "furigana", furigana_slist);

	return furigana_slist;
}
#endif


/* imcontext_reset() commit all preedit string and clear the buffers.
   It should not reset input mode.

   Applications need to call gtk_im_context_reset() when they get
   focus out event. And when each editable widget losed focus, apps
   need to call gtk_im_context_reset() too to commit the string.
 */
static void imcontext_reset(GtkIMContext* context)
{
	IMContextWhiz* cn = IM_CONTEXT_WHIZ(context);

	if (cn->kslength) {
		memset(cn->workbuf, 0, BUFSIZ);
		strncpy(cn->workbuf, (char*)cn->ks.echoStr, cn->kslength);
		gchar* utf8 = euc2utf8(cn->workbuf);
		g_signal_emit_by_name(cn, "commit", utf8);
		cn->kslength = 0;
		g_free(utf8);
	}

	memset(cn->workbuf, 0, BUFSIZ);
	memset(cn->scommit, 0, BUFSIZ);
	g_signal_emit_by_name(cn, "preedit_changed");
}

/* imcontext_kakutei() just do kakutei, commit and flush.
 * This works for generic canna usage, but doesn't for furigana support.
 */
/*static void imcontext_kakutei(IMContextWhiz* cn)
{
	jrKanjiStatusWithValue ksv;
	guchar buf[BUFSIZ];
	int len = 0;
	gchar* utf8 = NULL;

	ksv.ks = &cn->ks;
	ksv.buffer = buf;
	ksv.bytes_buffer = BUFSIZ;

	len = jrKanjiControl(cn->canna_context, KC_KAKUTEI, (void*)&ksv);
	utf8 = euc2utf8(buf);

	g_signal_emit_by_name(cn, "commit", utf8);
	memset(cn->workbuf, 0, BUFSIZ);
	memset(cn->scommit, 0, BUFSIZ);
	g_signal_emit_by_name(cn, "preedit_changed");

	g_free(utf8);
}

static void imcontext_kill(IMContextWhiz* cn)
{
	jrKanjiStatusWithValue ksv;
	guchar buf[BUFSIZ];
	int len = 0;
	gchar* utf8 = NULL;

	ksv.ks = &cn->ks;
	ksv.buffer = buf;
	ksv.bytes_buffer = BUFSIZ;

	len = jrKanjiControl(cn->canna_context, KC_KILL, (void*)&ksv);
	utf8 = euc2utf8(buf);

	memset(cn->workbuf, 0, BUFSIZ);
	memset(cn->scommit, 0, BUFSIZ);
	g_signal_emit_by_name(cn, "preedit_changed");

	jrKanjiControl(cn->canna_context, KC_INITIALIZE, 0);

	g_free(utf8);
}*/


static void scroll_cb(GtkWidget* widget, GdkEventScroll* event, IMContextWhiz* cn)
{
	switch (event->direction) {
	case GDK_SCROLL_UP:
		jrKanjiString(cn->canna_context, 0x02, cn->scommit, BUFSIZ, &cn->ks);
		imcontext_update_candwin(cn);
		break;
	case GDK_SCROLL_DOWN:
		jrKanjiString(cn->canna_context, 0x06, cn->scommit, BUFSIZ, &cn->ks);
		imcontext_update_candwin(cn);
		break;
	/*case GDK_SCROLL_LEFT:
	case GDK_SCROLL_RIGHT:
	case GDK_SCROLL_SMOOTH:*/
	}
}

/*static gboolean snooper_func(GtkWidget* widget, GdkEventKey* event, gpointer data)
{
	if (focused_context) {
		return imcontext_filter_keypress(focused_context, event);
	}

	return FALSE;
}
static guint snooper_id = 0;*/

static void set_ic_client_window(IMContextWhiz *context, GdkWindow *client_window)
{
	if (!client_window) return;

	if (context->client_window) {
		g_object_unref(context->client_window);
		context->client_window = NULL;
	}

	if (client_window != NULL) context->client_window = g_object_ref(client_window);

//	if (context->slave) gtk_im_context_set_client_window(context->slave, client_window);
}
static void imcontext_set_client_window(GtkIMContext* context, GdkWindow *w)
{
	IMContextWhiz* cn = IM_CONTEXT_WHIZ(context);
	//cn->client_window = w;
	set_ic_client_window(cn, w);
}

// TreeViewのインライン入力では、入力を完了させるたびにdisposeが呼ばれるみたい
static void imcontext_init(GtkIMContext *im_context)
{
	IMContextWhiz *cn = IM_CONTEXT_WHIZ(im_context);

	cn->canna_context = (long long)cn;
	cn->cand_stat = 0;
	cn->kslength = 0;
	cn->workbuf = g_new0(gchar, BUFSIZ);
	cn->scommit = g_new0(gchar, BUFSIZ);
	cn->client_window = NULL;
	cn->focus_in_candwin_show = FALSE;
	cn->function_mode = FALSE;

	// 候補ウインドウ
//	cn->candwin = gtk_menu_new();
	cn->candwin = gtk_window_new(GTK_WINDOW_POPUP);
	cn->candlabel = gtk_label_new("");
	gtk_container_add(GTK_CONTAINER(cn->candwin), cn->candlabel);
	//cn->candwin_area.x = cn->candwin_area.y = 0;
	//cn->candwin_area.width = cn->candwin_area.height = 0;
	gtk_window_resize(GTK_WINDOW(cn->candwin), 1, 1);

	gtk_widget_add_events(cn->candwin, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(cn->candwin, "scroll_event", G_CALLBACK(scroll_cb), cn);

	cn->layout  = gtk_widget_create_pango_layout(cn->candwin, "");

	gtk_window_set_accept_focus(GTK_WINDOW(cn->candwin), FALSE);

	jrKanjiControl(cn->canna_context, KC_INITIALIZE, 0);

	cn->modewin = gtk_window_new(GTK_WINDOW_POPUP);
	cn->modelabel = gtk_label_new("");
	gtk_container_add(GTK_CONTAINER(cn->modewin), cn->modelabel);

//	snooper_id = gtk_key_snooper_install((GtkKeySnoopFunc)snooper_func, NULL);

	imcontext_force_to_kana_mode(cn);
}

static void imcontext_finalize(GObject *obj)
{
	IMContextWhiz* cn = IM_CONTEXT_WHIZ(obj);

//	imcontext_set_client_window(GTK_IM_CONTEXT(cn), NULL);

	jrKanjiControl(cn->canna_context, KC_FINALIZE, 0);

/*	if (snooper_id != 0) {
		gtk_key_snooper_remove(snooper_id);
		snooper_id = 0;
	}*/

	g_free(cn->workbuf);
	g_free(cn->scommit);
	gtk_widget_destroy(GTK_WIDGET(cn->candlabel));
	gtk_widget_destroy(GTK_WIDGET(cn->candwin));
	g_object_unref(cn->layout);
	gtk_widget_destroy(cn->modelabel);
	gtk_widget_destroy(cn->modewin);
}

static void imcontext_class_init(GtkIMContextClass *c)
{
	GtkIMContextClass *im_context_class = GTK_IM_CONTEXT_CLASS(c);
	GObjectClass *object_class = G_OBJECT_CLASS(c);

	im_context_class->set_client_window = imcontext_set_client_window;

	/* Use snooper */
	im_context_class->filter_keypress = imcontext_filter_keypress;

	im_context_class->get_preedit_string = imcontext_get_preedit_string;
	im_context_class->set_cursor_location = imcontext_set_cursor_location;
	im_context_class->focus_in = imcontext_focus_in;
	im_context_class->focus_out = imcontext_focus_out;
	im_context_class->reset = imcontext_reset;

	object_class->finalize = imcontext_finalize;
}


////////////////////////////////////////////////////////
// モジュールがexportする関数

const char ContextId[] = CONTEXT_ID;
const char ContextName[] = CONTEXT_NAME;
const char DomainName[] = IMDOMAIN;
const char RegisterName[] = "IMContextWhiz";

GtkIMContextInfo im_info = {
	.context_id = ContextId,
	.context_name = ContextName,
	.domain = DomainName,
	.domain_dirname = IM_LOCALEDIR,
	.default_locales = "ja:ko:th:zh" //"*"
};

GtkIMContextInfo *info_list[] = {
	&im_info
};

void im_module_init(GTypeModule* module)
{
	GTypeInfo info = {
		.class_size = sizeof(IMContextWhizClass),
		.base_init = NULL,
		.base_finalize = NULL,

		.class_init = (GClassInitFunc)imcontext_class_init,
		.class_finalize = NULL,
		.class_data = NULL,

		.instance_size = sizeof(IMContextWhiz),
		.n_preallocs = 0,
		.instance_init = (typeof(info.instance_init))imcontext_init,

		.value_table = NULL
	};

	type_whiz = g_type_module_register_type(module, GTK_TYPE_IM_CONTEXT, RegisterName, &info, (GTypeFlags)0);
//	LOG(IMDOMAIN "\n");
}

void im_module_exit()
{
//	LOG(IMDOMAIN "\n");
}

void im_module_list(GtkIMContextInfo*** contexts, int* n_contexts)
{
	*contexts = info_list;
	*n_contexts = G_N_ELEMENTS(info_list);
}

GtkIMContext* im_module_create(const gchar* context_id)
{
	return strcmp(context_id, ContextId) == 0 ? GTK_IM_CONTEXT(g_object_new(type_whiz, NULL)) : NULL;
}
