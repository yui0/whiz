/* Shared GTK 3/4 candidate panel. Input focus stays with the client.
 * Hallmark · component: candidate list · tone: utilitarian · accent: blue
 * Native GTK CSS uses named colors because it does not support CSS variables.
 */
#ifndef WHIZ_IM_CANDIDATE_UI_H
#define WHIZ_IM_CANDIDATE_UI_H

#define WHIZ_PAGE_SIZE 10

typedef struct {
	GtkWidget *root, *reading, *status, *hint;
	GtkWidget *rows[WHIZ_PAGE_SIZE], *numbers[WHIZ_PAGE_SIZE], *words[WHIZ_PAGE_SIZE];
	int first;
	void (*activate)(int, gpointer);
	gpointer data;
} WhizCandidateView;

static void whiz_ui_class(GtkWidget *w, const char *name, gboolean enabled)
{
#if GTK_MAJOR_VERSION >= 4
	if (enabled) gtk_widget_add_css_class(w, name);
	else gtk_widget_remove_css_class(w, name);
#else
	GtkStyleContext *style = gtk_widget_get_style_context(w);
	if (enabled) gtk_style_context_add_class(style, name);
	else gtk_style_context_remove_class(style, name);
#endif
}

static void whiz_ui_append(GtkWidget *box, GtkWidget *child)
{
#if GTK_MAJOR_VERSION >= 4
	gtk_box_append(GTK_BOX(box), child);
#else
	gtk_box_pack_start(GTK_BOX(box), child, FALSE, FALSE, 0);
	gtk_widget_show(child);
#endif
}

static void apply_whiz_theme(void)
{
	static gsize initialized;
	if (!g_once_init_enter(&initialized)) return;
	static const char css[] =
		"@define-color whiz_paper #fbfbfc;"
		"@define-color whiz_ink #1d1d1f;"
		"@define-color whiz_muted #86868b;"
		"@define-color whiz_rule #d2d2d7;"
		"@define-color whiz_hover #ececf0;"
		"@define-color whiz_accent #0a64ff;"
		"@define-color whiz_pressed #0050d6;"
		"@define-color whiz_hud #2c2c2e;"
		"window.whiz-popup { background-color: @whiz_paper; border: 1px solid @whiz_rule; border-radius: 10px; }"
		"window.whiz-popup.whiz-hud { background-color: @whiz_hud; border: 0; border-radius: 12px; }"
		"popover.whiz-popup > contents { background: @whiz_paper; padding: 0; border: 1px solid @whiz_rule; border-radius: 10px; box-shadow: 0 6px 20px rgba(0,0,0,0.22); }"
		"popover.whiz-popup.whiz-hud > contents { background: @whiz_hud; border: 0; border-radius: 12px; }"
		".whiz-candidates { padding: 4px; color: @whiz_ink; font-family: \"Hiragino Sans\", \"Noto Sans CJK JP\", \"Noto Sans JP\", sans-serif; font-size: 16px; }"
		".whiz-reading { color: @whiz_muted; font-size: 12px; padding: 2px 8px 4px; }"
		"button.whiz-row { background: transparent; color: @whiz_ink; border: 0; border-radius: 6px; box-shadow: none; text-shadow: none; padding: 1px 6px; min-height: 22px; }"
		"button.whiz-row:hover { background: @whiz_hover; }"
		"button.whiz-row label { color: @whiz_ink; }"
		"button.whiz-row.selected { background: @whiz_accent; color: @whiz_paper; }"
		"button.whiz-row.selected label, button.whiz-row:active label { color: @whiz_paper; }"
		"button.whiz-row:active { background: @whiz_pressed; color: @whiz_paper; }"
		"button.whiz-row label.whiz-number { color: @whiz_muted; font-size: 13px; }"
		".whiz-row.selected .whiz-number, .whiz-row:active .whiz-number { color: @whiz_paper; }"
		".whiz-status { color: @whiz_muted; font-size: 11px; padding: 4px 8px 2px; border-top: 1px solid @whiz_rule; margin-top: 3px; }"
		".whiz-hint { color: @whiz_muted; font-size: 11px; padding: 0 8px 3px; }"
		".whiz-mode { background: transparent; color: #ffffff; padding: 6px 14px; font-size: 26px; font-weight: 500; min-width: 36px; font-family: \"Hiragino Sans\", \"Noto Sans CJK JP\", \"Noto Sans JP\", sans-serif; }";
	GtkCssProvider *provider = gtk_css_provider_new();
#if GTK_MAJOR_VERSION >= 4
#if GTK_CHECK_VERSION(4, 12, 0)
	gtk_css_provider_load_from_string(provider, css);
#else
	gtk_css_provider_load_from_data(provider, css, -1);
#endif
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
		GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#else
	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#endif
	g_object_unref(provider);
	g_once_init_leave(&initialized, 1);
}

static void whiz_ui_clicked(GtkButton *button, gpointer data)
{
	WhizCandidateView *view = data;
	int row = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "whiz-row"));
	view->activate(view->first + row, view->data);
}

static void whiz_ui_init(WhizCandidateView *view,
	                    void (*activate)(int, gpointer), gpointer data)
{
	view->activate = activate;
	view->data = data;
	view->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	whiz_ui_class(view->root, "whiz-candidates", TRUE);
	gtk_widget_set_size_request(view->root, 200, -1);
	view->reading = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(view->reading), 0);
	gtk_label_set_ellipsize(GTK_LABEL(view->reading), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars(GTK_LABEL(view->reading), 28);
	whiz_ui_class(view->reading, "whiz-reading", TRUE);
	whiz_ui_append(view->root, view->reading);
	for (int i = 0; i < WHIZ_PAGE_SIZE; i++) {
		GtkWidget *row = gtk_button_new();
		view->rows[i] = row;
		whiz_ui_class(row, "whiz-row", TRUE);
		gtk_widget_set_focus_on_click(row, FALSE);
#if GTK_MAJOR_VERSION >= 4
		gtk_widget_set_focusable(row, FALSE);
#else
		gtk_widget_set_can_focus(row, FALSE);
		gtk_widget_set_no_show_all(row, TRUE);
#endif
		GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		char number[4];
		g_snprintf(number, sizeof(number), "%d", (i + 1) % 10);
		view->numbers[i] = gtk_label_new(number);
		gtk_widget_set_size_request(view->numbers[i], 16, -1);
		whiz_ui_class(view->numbers[i], "whiz-number", TRUE);
		view->words[i] = gtk_label_new("");
		gtk_label_set_xalign(GTK_LABEL(view->words[i]), 0);
		gtk_label_set_ellipsize(GTK_LABEL(view->words[i]), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(view->words[i]), 24);
		whiz_ui_append(box, view->numbers[i]);
		whiz_ui_append(box, view->words[i]);
#if GTK_MAJOR_VERSION >= 4
		gtk_button_set_child(GTK_BUTTON(row), box);
#else
		gtk_container_add(GTK_CONTAINER(row), box);
		gtk_widget_show(box);
#endif
		whiz_ui_append(view->root, row);
		g_object_set_data(G_OBJECT(row), "whiz-row", GINT_TO_POINTER(i));
		g_signal_connect(row, "clicked", G_CALLBACK(whiz_ui_clicked), view);
	}
	view->status = gtk_label_new("");
	whiz_ui_class(view->status, "whiz-status", TRUE);
	whiz_ui_append(view->root, view->status);
	view->hint = NULL;
}

/* Stride permits prediction and conversion buffers to share one view. */
static void whiz_ui_update(WhizCandidateView *view, const char *reading,
	const char *words, size_t stride, int count, int selected, int segment, int segments)
{
	view->first = (selected >= 0 ? selected / WHIZ_PAGE_SIZE : 0) * WHIZ_PAGE_SIZE;
	gtk_label_set_text(GTK_LABEL(view->reading), reading);
	for (int row = 0; row < WHIZ_PAGE_SIZE; row++) {
		int index = view->first + row;
		gtk_widget_set_visible(view->rows[row], index < count);
		if (index >= count) continue;
		const char *word = words + index * stride;
		gtk_label_set_text(GTK_LABEL(view->words[row]), word);
		gtk_widget_set_tooltip_text(view->rows[row], word);
		/* Digits are shortcuts only while converting, never while typing. */
		char number[4] = "";
		if (segments > 0) g_snprintf(number, sizeof(number), "%d", (row + 1) % 10);
		gtk_label_set_text(GTK_LABEL(view->numbers[row]), number);
		whiz_ui_class(view->rows[row], "selected", index == selected);
	}
	char status[48];
	if (segments > 0)
		g_snprintf(status, sizeof(status), "文節 %d/%d　%d/%d", segment + 1, segments,
			selected + 1, count);
	else
		g_snprintf(status, sizeof(status), "%d/%d", selected + 1, count);
	gtk_label_set_text(GTK_LABEL(view->status), status);
	/* Kotoeri shows no key help; keep it as a tooltip for discoverability. */
	gtk_widget_set_tooltip_text(view->status, segments > 0 ?
		"↑↓ 選択　Enter 確定　Esc 戻る　←→ 文節　PgUp/PgDn ページ" :
		"↑↓ / Tab 選択　Enter 確定　Space 変換　Esc 戻る");
}

#endif
