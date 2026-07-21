/*
 * Copyright © 2026 Yuichiro Nakada / Project Whiz
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//---------------------------------------------------------
//	whiz — kana→kanji via whiz.dic lattice + unified CostNet
//
//	Matches apps/whiz/engine/whiz.py:
//	  cost = learned_word + learned_connect(left,right) + longest-match terms
//	  learned word/connect costs are both stored in model.wcnn
//
//	Build:  make
//	Export: python3 whiz.py export   → out/model.wcnn
//	Run:    ./whiz -m out/model.wcnn -d ../dic/whiz.dic -p "とうきょうにいきます"
//---------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#ifndef __EMSCRIPTEN__
#include <iconv.h>
#endif

#define MAX_YOMI       384
#define MAX_BEAM       40
#define MAX_PATH       48
#define MAX_WORD       96
#define MAX_READ_KEY   96
#define MAX_EDGES_POS  64
#define N_HASH_DEFAULT 4096
#define BOS_WCLASS     0
#define EOS_WCLASS     1

// ─── UTF-8 ──────────────────────────────────────────────────────────────────

static int utf8_decode(const unsigned char *p, int *len)
{
	if (p[0] < 0x80) { *len = 1; return p[0]; }
	if ((p[0] & 0xE0) == 0xC0) {
		*len = 2;
		return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
	}
	if ((p[0] & 0xF0) == 0xE0) {
		*len = 3;
		return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
	}
	if ((p[0] & 0xF8) == 0xF0) {
		*len = 4;
		return ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12)
			| ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
	}
	*len = 1;
	return p[0];
}

static int utf8_encode(int cp, unsigned char *out)
{
	if (cp < 0x80) { out[0] = (unsigned char)cp; return 1; }
	if (cp < 0x800) {
		out[0] = 0xC0 | (cp >> 6);
		out[1] = 0x80 | (cp & 0x3F);
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = 0xE0 | (cp >> 12);
		out[1] = 0x80 | ((cp >> 6) & 0x3F);
		out[2] = 0x80 | (cp & 0x3F);
		return 3;
	}
	out[0] = 0xF0 | (cp >> 18);
	out[1] = 0x80 | ((cp >> 12) & 0x3F);
	out[2] = 0x80 | ((cp >> 6) & 0x3F);
	out[3] = 0x80 | (cp & 0x3F);
	return 4;
}

static int utf8_clen(const unsigned char *p)
{
	int len = 0;
	utf8_decode(p, &len);
	return len > 0 ? len : 1;
}

static int utf8_nchars(const char *s)
{
	const unsigned char *p = (const unsigned char *)s;
	int n = 0;
	while (*p) { p += utf8_clen(p); n++; }
	return n;
}

static void utf8_slice(const char *s, int char_start, int nchar, char *buf, int bufsz)
{
	const unsigned char *p = (const unsigned char *)s;
	int ci = 0;
	while (*p && ci < char_start) { p += utf8_clen(p); ci++; }
	int o = 0;
	for (int i = 0; i < nchar && *p && o + 4 < bufsz; i++) {
		int L = utf8_clen(p);
		memcpy(buf + o, p, (size_t)L);
		o += L;
		p += L;
	}
	buf[o] = 0;
}

static void hira_to_kata(const char *in, char *out, int outsz)
{
	const unsigned char *p = (const unsigned char *)in;
	int o = 0;
	while (*p && o + 4 < outsz) {
		int len = 0;
		int cp = utf8_decode(p, &len);
		p += len;
		if (cp >= 0x3041 && cp <= 0x3096) cp += 0x60;
		o += utf8_encode(cp, (unsigned char *)out + o);
	}
	out[o] = 0;
}

static void kata_to_hira(const char *in, char *out, int outsz)
{
	const unsigned char *p = (const unsigned char *)in;
	int o = 0;
	while (*p && o + 4 < outsz) {
		int len = 0;
		int cp = utf8_decode(p, &len);
		p += len;
		if (cp >= 0x30A1 && cp <= 0x30F6) cp -= 0x60;
		o += utf8_encode(cp, (unsigned char *)out + o);
	}
	out[o] = 0;
}

static int is_small_kana(int cp)
{
	return cp == 0x30A1 || cp == 0x30A3 || cp == 0x30A5 || cp == 0x30A7 || cp == 0x30A9
		|| cp == 0x30E3 || cp == 0x30E5 || cp == 0x30E7
		|| cp == 0x30C3 || cp == 0x30FC
		|| cp == 0x3041 || cp == 0x3043 || cp == 0x3045 || cp == 0x3047 || cp == 0x3049
		|| cp == 0x3083 || cp == 0x3085 || cp == 0x3087
		|| cp == 0x3063;
}

static int peek_cp_at_char(const char *s, int char_idx)
{
	const unsigned char *p = (const unsigned char *)s;
	int ci = 0;
	while (*p && ci < char_idx) { p += utf8_clen(p); ci++; }
	if (!*p) return 0;
	int len = 0;
	return utf8_decode(p, &len);
}

static int is_alphabet_cp(int cp)
{
	return (cp >= '0' && cp <= '9')
		|| (cp >= 'A' && cp <= 'Z')
		|| (cp >= 'a' && cp <= 'z')
		|| (cp >= 0xFF10 && cp <= 0xFF19)
		|| (cp >= 0xFF21 && cp <= 0xFF3A)
		|| (cp >= 0xFF41 && cp <= 0xFF5A);
}

static int is_katakana_cp(int cp)
{
	return (cp >= 0x30A0 && cp <= 0x30FF)
		|| (cp >= 0x31F0 && cp <= 0x31FF)
		|| (cp >= 0xFF66 && cp <= 0xFF9F);
}

static int passthrough_kind_at(const char *s, int char_idx)
{
	int cp = peek_cp_at_char(s, char_idx);
	if (is_alphabet_cp(cp)) return 1;
	if (is_katakana_cp(cp)) return 2;
	return 0;
}

static int span_has_passthrough(const char *s, int start, int nchar)
{
	for (int i = 0; i < nchar; i++)
		if (passthrough_kind_at(s, start + i)) return 1;
	return 0;
}

static uint32_t adler32_py(const unsigned char *data, size_t len)
{
	uint32_t a = 1, b = 0;
	const uint32_t MOD = 65521;
	for (size_t i = 0; i < len; i++) {
		a = (a + data[i]) % MOD;
		b = (b + a) % MOD;
	}
	return (b << 16) | a;
}

static int surface_hash(const char *word, int n_hash)
{
	if (!word || !word[0] || (word[0] == '*' && word[1] == 0)) return 0;
	return (int)(adler32_py((const unsigned char *)word, strlen(word)) % (uint32_t)n_hash);
}

static int is_particle(const char *word)
{
	static const char *ps[] = {
		"は", "が", "を", "に", "で", "と", "も", "へ", "の", "や",
		"か", "ね", "よ", "わ", NULL
	};
	for (int i = 0; ps[i]; i++)
		if (strcmp(word, ps[i]) == 0) return 1;
	return 0;
}

// ─── ConnectNet (.wcnn) ─────────────────────────────────────────────────────

typedef struct {
	uint32_t dim, hidden, n_pos, n_type, n_form, n_hash, n_words;
	float mora, mora2, frag, short_pen, short_margin;
	float rare_cost, rare_long, uni_floor;
	float *pos_emb, *type_emb, *form_emb, *hash_emb;
	float *word_cost;       /* [n_words] learned lexical costs */
	float *m0_w, *m0_b;   /* [H, 8D], [H] */
	float *m1_w, *m1_b;   /* [H, H], [H] */
	float *m2_w, *m2_b;   /* [1, H], [1] */
} ConnectNet;

static float *read_f32_mem(const uint8_t **pp, const uint8_t *end, size_t n)
{
	size_t bytes = n * sizeof(float);
	if (*pp + bytes > end) return NULL;
	float *p = (float *)malloc(bytes);
	if (!p) return NULL;
	memcpy(p, *pp, bytes);
	*pp += bytes;
	return p;
}

static int read_u32_mem(const uint8_t **pp, const uint8_t *end, uint32_t *o)
{
	if (*pp + 4 > end) return 1;
	memcpy(o, *pp, 4);
	*pp += 4;
	return 0;
}

static int read_f32_one_mem(const uint8_t **pp, const uint8_t *end, float *o)
{
	if (*pp + 4 > end) return 1;
	memcpy(o, *pp, 4);
	*pp += 4;
	return 0;
}

static int wcnn_load_mem(const uint8_t *buf, size_t sz, ConnectNet *m)
{
	memset(m, 0, sizeof(*m));
	const uint8_t *p = buf, *end = buf + sz;
	if (sz < 8 || memcmp(p, "WCNN", 4) != 0) {
		fprintf(stderr, "bad WCNN magic\n");
		return 1;
	}
	p += 4;
	uint32_t ver = 0;
	if (read_u32_mem(&p, end, &ver) || ver != 2) {
		fprintf(stderr, "unsupported WCNN version\n");
		return 1;
	}
	if (read_u32_mem(&p, end, &m->dim) || read_u32_mem(&p, end, &m->hidden)
		|| read_u32_mem(&p, end, &m->n_pos) || read_u32_mem(&p, end, &m->n_type)
		|| read_u32_mem(&p, end, &m->n_form) || read_u32_mem(&p, end, &m->n_hash)
		|| read_u32_mem(&p, end, &m->n_words))
		return 1;
	if (read_f32_one_mem(&p, end, &m->mora) || read_f32_one_mem(&p, end, &m->mora2)
		|| read_f32_one_mem(&p, end, &m->frag) || read_f32_one_mem(&p, end, &m->short_pen)
		|| read_f32_one_mem(&p, end, &m->short_margin) || read_f32_one_mem(&p, end, &m->rare_cost)
		|| read_f32_one_mem(&p, end, &m->rare_long) || read_f32_one_mem(&p, end, &m->uni_floor))
		return 1;

	uint32_t D = m->dim, H = m->hidden;
	size_t in8 = (size_t)D * 8;
	m->pos_emb  = read_f32_mem(&p, end, (size_t)m->n_pos * D);
	m->type_emb = read_f32_mem(&p, end, (size_t)m->n_type * D);
	m->form_emb = read_f32_mem(&p, end, (size_t)m->n_form * D);
	m->hash_emb = read_f32_mem(&p, end, (size_t)m->n_hash * D);
	m->word_cost = read_f32_mem(&p, end, m->n_words);
	m->m0_w = read_f32_mem(&p, end, (size_t)H * in8);
	m->m0_b = read_f32_mem(&p, end, H);
	m->m1_w = read_f32_mem(&p, end, (size_t)H * H);
	m->m1_b = read_f32_mem(&p, end, H);
	m->m2_w = read_f32_mem(&p, end, H);
	m->m2_b = read_f32_mem(&p, end, 1);
	if (!m->pos_emb || !m->word_cost || !m->m2_b) {
		fprintf(stderr, "WCNN truncated\n");
		return 1;
	}
	fprintf(stderr, "costnet: dim=%u hidden=%u words=%u\n",
		m->dim, m->hidden, m->n_words);
	return 0;
}

#if !defined(__EMSCRIPTEN__) && !defined(WHIZ_LIBRARY)
static int wcnn_load(const char *path, ConnectNet *m)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) { perror(path); return 1; }
	fseek(fp, 0, SEEK_END);
	long sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	uint8_t *buf = (uint8_t *)malloc((size_t)sz);
	if (!buf || fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
		fclose(fp); free(buf); return 1;
	}
	fclose(fp);
	int r = wcnn_load_mem(buf, (size_t)sz, m);
	free(buf);
	return r;
}
#endif

static void wcnn_free(ConnectNet *m)
{
	if (!m) return;
	free(m->pos_emb); free(m->type_emb); free(m->form_emb); free(m->hash_emb);
	free(m->word_cost);
	free(m->m0_w); free(m->m0_b);
	free(m->m1_w); free(m->m1_b);
	free(m->m2_w); free(m->m2_b);
}

static float gelu_f(float x)
{
	/* match torch.nn.GELU (erf) */
	return 0.5f * x * (1.f + erff(x * 0.7071067811865475f));
}

static int idx_type(int t, uint32_t n_type)
{
	if (t < 0) return 0;
	int v = t + 1;
	if (v >= (int)n_type) v = (int)n_type - 1;
	return v;
}

static int idx_form(int f, uint32_t n_form)
{
	if (f < 0) return 0;
	int v = f + 1;
	if (v >= (int)n_form) v = (int)n_form - 1;
	return v;
}

static int idx_pos(int p, uint32_t n_pos)
{
	if (p < 0) return 0;
	if (p >= (int)n_pos) return (int)n_pos - 1;
	return p;
}

static void encode_side(
	ConnectNet *m, int wclass, int typ, int form, int hid, float *out)
{
	uint32_t D = m->dim;
	int ip = idx_pos(wclass, m->n_pos);
	int it = idx_type(typ, m->n_type);
	int iff = idx_form(form, m->n_form);
	if (hid < 0) hid = 0;
	if ((uint32_t)hid >= m->n_hash) hid = (int)m->n_hash - 1;
	memcpy(out + 0 * D, m->pos_emb + (size_t)ip * D, D * sizeof(float));
	memcpy(out + 1 * D, m->type_emb + (size_t)it * D, D * sizeof(float));
	memcpy(out + 2 * D, m->form_emb + (size_t)iff * D, D * sizeof(float));
	memcpy(out + 3 * D, m->hash_emb + (size_t)hid * D, D * sizeof(float));
}

static float connect_cost(
	ConnectNet *m,
	int lw, int lt, int lf, int lh,
	int rw, int rt, int rf, int rh)
{
	uint32_t D = m->dim, H = m->hidden;
	float left[4 * 128], right[4 * 128], cat[8 * 128];
	float h0[256], h1[256];
	if (D > 128 || H > 256) return 2500.f;
	encode_side(m, lw, lt, lf, lh, left);
	encode_side(m, rw, rt, rf, rh, right);
	memcpy(cat, left, 4 * D * sizeof(float));
	memcpy(cat + 4 * D, right, 4 * D * sizeof(float));

	size_t in8 = (size_t)D * 8;
	for (uint32_t j = 0; j < H; j++) {
		float s = m->m0_b[j];
		const float *row = m->m0_w + (size_t)j * in8;
		for (size_t i = 0; i < in8; i++) s += row[i] * cat[i];
		h0[j] = gelu_f(s);
	}
	for (uint32_t j = 0; j < H; j++) {
		float s = m->m1_b[j];
		const float *row = m->m1_w + (size_t)j * H;
		for (uint32_t i = 0; i < H; i++) s += row[i] * h0[i];
		h1[j] = gelu_f(s);
	}
	float out = m->m2_b[0];
	for (uint32_t j = 0; j < H; j++) out += m->m2_w[j] * h1[j];
	if (out < 0.f) out = 0.f;
	if (out > 8000.f) out = 8000.f;
	return out;
}

// ─── Dictionary ─────────────────────────────────────────────────────────────

typedef struct {
	char *read;
	char *word;
	int32_t idx;
	int16_t wclass;
	int16_t cost;
	int16_t typ;
	int16_t form;
} DicEntry;

typedef struct {
	DicEntry *entries;
	int n;
	int max_read_chars;
	int *order;       /* indices sorted by read (katakana) */
	int *word_order;  /* indices sorted by word (surface) */
} WhizDic;

static WhizDic *g_sort_dic;

static int dic_cmp_order(const void *a, const void *b)
{
	int ia = *(const int *)a, ib = *(const int *)b;
	return strcmp(g_sort_dic->entries[ia].read, g_sort_dic->entries[ib].read);
}

static int dic_cmp_word_order(const void *a, const void *b)
{
	int ia = *(const int *)a, ib = *(const int *)b;
	return strcmp(g_sort_dic->entries[ia].word, g_sort_dic->entries[ib].word);
}

#ifdef __EMSCRIPTEN__
static int eucjp_to_utf8(const char *in, size_t inlen, char *out, size_t outsz)
{
	(void)in; (void)inlen; (void)out; (void)outsz;
	return -1; /* browser path uses WUTF (UTF-8); no iconv */
}
#else
static int eucjp_to_utf8(const char *in, size_t inlen, char *out, size_t outsz)
{
	iconv_t cd = iconv_open("UTF-8", "EUC-JP");
	if (cd == (iconv_t)-1) return -1;
	char *ip = (char *)in;
	char *op = out;
	size_t il = inlen, ol = outsz - 1;
	size_t r = iconv(cd, &ip, &il, &op, &ol);
	iconv_close(cd);
	if (r == (size_t)-1) return -1;
	*op = 0;
	return (int)(op - out);
}
#endif

static void dic_add(
	WhizDic *d, int *cap,
	const char *read, const char *word,
	int wclass, int cost, int typ, int form)
{
	if (d->n >= *cap) {
		*cap *= 2;
		d->entries = (DicEntry *)realloc(d->entries, (size_t)(*cap) * sizeof(DicEntry));
	}
	DicEntry *e = &d->entries[d->n];
	e->idx = d->n++;
	e->read = strdup(read);
	e->word = strdup(word);
	e->wclass = (int16_t)wclass;
	e->cost = (int16_t)cost;
	e->typ = (int16_t)typ;
	e->form = (int16_t)form;
	int rc = utf8_nchars(e->read);
	if (rc > d->max_read_chars) d->max_read_chars = rc;
}

static void inject_func(WhizDic *d, int *cap)
{
	/* mirrors FUNC_ENTRIES in whiz.py */
	struct { const char *r, *w; int wc, c, t, f; } F[] = {
		{"デス", "です", 75, 150, 55, 1},
		{"マス", "ます", 75, 150, 57, 1},
		{"イイ", "いい", 52, 300, 69, 1},
		{"イイ", "良い", 52, 450, 69, 1},
		{"イキ", "行き", 48, 400, 8, 12},
		{"イキ", "いき", 48, 700, 8, 12},
		{"シ", "し", 48, 200, 2, 12},
		{"シマス", "します", 75, 200, 57, 1},
		{"ハ", "は", 66, 0, -1, -1},
		{"ガ", "が", 66, 0, -1, -1},
		{"ヲ", "を", 62, 0, -1, -1},
		{"ニ", "に", 62, 0, -1, -1},
		{"デ", "で", 62, 120, -1, -1},
		{"ト", "と", 62, 0, -1, -1},
		{"モ", "も", 66, 0, -1, -1},
		{"ノ", "の", 72, 0, -1, -1},
		{"ヘ", "へ", 62, 0, -1, -1},
		{"テ", "て", 65, 80, -1, -1},
		{"タ", "た", 75, 250, 53, 1},
		{"ワタシ", "私", 15, 80, -1, -1},
		{"ワタシ", "わたし", 15, 200, -1, -1},
		/* ─ しょう / でしょう 系 ─ */
		{"ショウ",       "しょう",       75,  80, -1, -1},
		{"デショウ",     "でしょう",     75,  60, -1, -1},
		{"デショウカ",   "でしょうか",   75,  60, -1, -1},
		{"マショウ",     "ましょう",     75,  80, -1, -1},
		{"マショウカ",   "ましょうか",   75,  80, -1, -1},
		{"デショ",       "でしょ",       75, 120, -1, -1},
		/* ─ ない / ません 系 ─ */
		{"ナイ",         "ない",         52, 120, -1, -1},
		{"ナイデ",       "ないで",       65, 180, -1, -1},
		{"ナカッタ",     "なかった",     75, 180, -1, -1},
		{"マセン",       "ません",       75, 100, -1, -1},
		{"マセンデシタ", "ませんでした", 75, 100, -1, -1},
		/* ─ ている / てる 系 ─ */
		{"テイル",       "ている",       48, 180, -1, -1},
		{"テル",         "てる",         48, 250, -1, -1},
		{"テイナイ",     "ていない",     48, 180, -1, -1},
		{"テイマス",     "ています",     75, 130, -1, -1},
		{"テイマセン",   "ていません",   75, 130, -1, -1},
		/* ─ だろう 系 ─ */
		{"ダロウ",       "だろう",       75, 120, -1, -1},
		{"ダロウカ",     "だろうか",     75, 120, -1, -1},
		/* ─ じゃ / ちゃ 縮約形 ─ */
		{"ジャ",         "じゃ",         62, 180, -1, -1},
		{"ジャナイ",     "じゃない",     52, 200, -1, -1},
		{"チャ",         "ちゃ",         65, 250, -1, -1},
		{NULL, NULL, 0, 0, 0, 0}
	};
	int n0 = d->n;
	for (int i = 0; F[i].r; i++)
		dic_add(d, cap, F[i].r, F[i].w, F[i].wc, F[i].c, F[i].t, F[i].f);
	fprintf(stderr, "func lexicon: +%d entries\n", d->n - n0);
}

static void whiz_finalize_index(WhizDic *d)
{
	d->order = (int *)malloc((size_t)d->n * sizeof(int));
	d->word_order = (int *)malloc((size_t)d->n * sizeof(int));
	for (int i = 0; i < d->n; i++) d->order[i] = d->word_order[i] = i;
	g_sort_dic = d;
	qsort(d->order, (size_t)d->n, sizeof(int), dic_cmp_order);
	qsort(d->word_order, (size_t)d->n, sizeof(int), dic_cmp_word_order);
	fprintf(stderr, "whiz.dic: %d entries, max_read_chars=%d\n", d->n, d->max_read_chars);
}

/* Web-friendly UTF-8 dictionary (magic WUTF) — no iconv needed */
static int whiz_load_wutf_mem(const uint8_t *buf, size_t sz, WhizDic *d)
{
	memset(d, 0, sizeof(*d));
	if (sz < 16 || memcmp(buf, "WUTF", 4) != 0) return 1;
	const uint8_t *p = buf + 4, *end = buf + sz;
	uint32_t ver = 0, n = 0, maxr = 0;
	if (read_u32_mem(&p, end, &ver) || ver != 1) return 1;
	if (read_u32_mem(&p, end, &n) || read_u32_mem(&p, end, &maxr)) return 1;
	int cap = (int)n + 64;
	if (cap < 1024) cap = 1024;
	d->entries = (DicEntry *)malloc((size_t)cap * sizeof(DicEntry));
	d->n = 0;
	d->max_read_chars = (int)maxr;
	for (uint32_t i = 0; i < n; i++) {
		if (p + 4 > end) return 1;
		uint16_t rlen = 0, wlen = 0;
		memcpy(&rlen, p, 2); p += 2;
		memcpy(&wlen, p, 2); p += 2;
		if (p + rlen + wlen + 8 > end) return 1;
		char read_u[512], word_u[512];
		if (rlen >= sizeof(read_u) || wlen >= sizeof(word_u)) return 1;
		memcpy(read_u, p, rlen); read_u[rlen] = 0; p += rlen;
		memcpy(word_u, p, wlen); word_u[wlen] = 0; p += wlen;
		int16_t wclass, cost, typ, form;
		memcpy(&wclass, p, 2); p += 2;
		memcpy(&cost, p, 2); p += 2;
		memcpy(&typ, p, 2); p += 2;
		memcpy(&form, p, 2); p += 2;
		dic_add(d, &cap, read_u, word_u, wclass, cost, typ, form);
	}
	/* WUTF already includes func lexicon from Python export */
	whiz_finalize_index(d);
	return 0;
}

static int whiz_load_euc_mem(const uint8_t *buf, size_t sz, WhizDic *d)
{
	memset(d, 0, sizeof(*d));
	int cap = 65536;
	d->entries = (DicEntry *)malloc((size_t)cap * sizeof(DicEntry));
	d->n = 0;
	d->max_read_chars = 1;
	char utf[512];
	size_t off = 0;
	while (off < sz) {
		size_t rstart = off;
		while (off < sz && buf[off] != 0) off++;
		if (off >= sz) break;
		size_t rlen = off - rstart;
		off++;
		size_t wstart = off;
		while (off < sz && buf[off] != 0) off++;
		if (off >= sz) break;
		size_t wlen = off - wstart;
		off++;
		if (off + 6 > sz) break;
		int16_t wclass, cost, inflect;
		memcpy(&wclass, buf + off, 2);
		memcpy(&cost, buf + off + 2, 2);
		memcpy(&inflect, buf + off + 4, 2);
		off += 6;
		if (rlen == 0 || wlen == 0 || rlen >= 400 || wlen >= 400) continue;
		if (eucjp_to_utf8((char *)buf + rstart, rlen, utf, sizeof(utf)) < 0) continue;
		char read_u[256];
		size_t read_len = strlen(utf);
		if (read_len >= sizeof(read_u)) continue;
		memcpy(read_u, utf, read_len + 1);
		if (eucjp_to_utf8((char *)buf + wstart, wlen, utf, sizeof(utf)) < 0) continue;
		dic_add(d, &cap, read_u, utf, wclass, cost, inflect, -1);
	}
	inject_func(d, &cap);
	whiz_finalize_index(d);
	return 0;
}

static int whiz_load_mem(const uint8_t *buf, size_t sz, WhizDic *d)
{
	if (sz >= 4 && memcmp(buf, "WUTF", 4) == 0)
		return whiz_load_wutf_mem(buf, sz, d);
	return whiz_load_euc_mem(buf, sz, d);
}

#if !defined(__EMSCRIPTEN__) && !defined(WHIZ_LIBRARY)
static int whiz_load(const char *path, WhizDic *d)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) { perror(path); return 1; }
	fseek(fp, 0, SEEK_END);
	long sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	uint8_t *buf = (uint8_t *)malloc((size_t)sz);
	if (!buf || fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
		fclose(fp); free(buf); return 1;
	}
	fclose(fp);
	int r = whiz_load_mem(buf, (size_t)sz, d);
	free(buf);
	return r;
}
#endif

static void whizdic_free(WhizDic *d)
{
	for (int i = 0; i < d->n; i++) {
		free(d->entries[i].read);
		free(d->entries[i].word);
	}
	free(d->entries);
	free(d->order);
	free(d->word_order);
}

typedef struct {
	char word[MAX_WORD];
	char read[MAX_READ_KEY];
	int wclass, typ, form, cost, hid, entry_idx;
	float lexical_cost;
	int start, end;
	int read_len; /* mora / char length of reading */
} Node;

static int bad_surface(const char *read, const char *word)
{
	static const char *bad[] = {
		"、", "。", "．", "，", "・", "：", "；", "！", "？", NULL
	};
	for (int i = 0; bad[i]; i++)
		if (strcmp(word, bad[i]) == 0) return 1;
	if (!word[0]) return 1;
	if ((unsigned char)word[0] < 0x80 && word[1] == 0 && read[0]) {
		int len = 0;
		int cp = utf8_decode((const unsigned char *)read, &len);
		if ((cp >= 0x30A0 && cp <= 0x30FF) || (cp >= 0x3040 && cp <= 0x309F))
			return 1;
	}
	return 0;
}

static void fill_node(
	Node *nd, const DicEntry *e, const char *span,
	int start, int end, ConnectNet *m)
{
	strncpy(nd->word, e->word, MAX_WORD - 1);
	nd->word[MAX_WORD - 1] = 0;
	strncpy(nd->read, span, MAX_READ_KEY - 1);
	nd->read[MAX_READ_KEY - 1] = 0;
	nd->wclass = e->wclass;
	nd->typ = e->typ;
	nd->form = e->form;
	nd->cost = e->cost;
	nd->entry_idx = e->idx;
	nd->lexical_cost = (e->idx >= 0 && (uint32_t)e->idx < m->n_words)
		? m->word_cost[e->idx] : (float)e->cost;
	if (nd->lexical_cost < 0.f) nd->lexical_cost = 0.f;
	if (nd->lexical_cost > 8000.f) nd->lexical_cost = 8000.f;
	nd->hid = surface_hash(e->word, (int)m->n_hash);
	nd->start = start;
	nd->end = end;
	nd->read_len = end - start;
}

static int collect_cands(
	WhizDic *d, ConnectNet *m, const char *span,
	int start, int end, Node *out, int out_cap)
{
	int lo = 0, hi = d->n;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		int c = strcmp(d->entries[d->order[mid]].read, span);
		if (c < 0) lo = mid + 1;
		else hi = mid;
	}
	int n = 0;
	for (int i = lo; i < d->n; i++) {
		DicEntry *e = &d->entries[d->order[i]];
		if (strcmp(e->read, span) != 0) break;
		if (bad_surface(span, e->word)) continue;
		if (n >= out_cap) break;
		fill_node(&out[n++], e, span, start, end, m);
	}
	return n;
}

static int has_dictionary_match(
	WhizDic *d, const char *yomi, const char *original,
	int start, int nchars)
{
	int lim = d->max_read_chars;
	if (lim > nchars - start) lim = nchars - start;
	if (lim > 24) lim = 24;
	for (int L = lim; L >= 1; L--) {
		if (span_has_passthrough(original, start, L)) continue;
		if (start + L < nchars
			&& is_small_kana(peek_cp_at_char(yomi, start + L)))
			continue;
		char span[MAX_READ_KEY];
		utf8_slice(yomi, start, L, span, sizeof(span));
		int lo = 0, hi = d->n;
		while (lo < hi) {
			int mid = (lo + hi) / 2;
			int c = strcmp(d->entries[d->order[mid]].read, span);
			if (c < 0) lo = mid + 1;
			else hi = mid;
		}
		for (int i = lo; i < d->n; i++) {
			DicEntry *e = &d->entries[d->order[i]];
			if (strcmp(e->read, span) != 0) break;
			if (!bad_surface(span, e->word)) return 1;
		}
	}
	return 0;
}

static void fill_unknown_node(
	Node *nd, const char *yomi, const char *original,
	int start, int end, ConnectNet *m)
{
	utf8_slice(original, start, end - start, nd->word, MAX_WORD);
	utf8_slice(yomi, start, end - start, nd->read, MAX_READ_KEY);
	nd->wclass = 3;
	nd->typ = -1;
	nd->form = -1;
	nd->cost = 3500;
	nd->entry_idx = -1;
	nd->lexical_cost = 3500.f;
	nd->hid = surface_hash(nd->word, (int)m->n_hash);
	nd->start = start;
	nd->end = end;
	nd->read_len = end - start;
}

static int node_len_cost_cmp(const void *a, const void *b)
{
	const Node *x = (const Node *)a, *y = (const Node *)b;
	if (x->read_len != y->read_len) return (y->read_len - x->read_len); /* longer first */
	return (x->lexical_cost > y->lexical_cost) - (x->lexical_cost < y->lexical_cost);
}

static int build_lattice(
	WhizDic *d, ConnectNet *m, const char *yomi, const char *original,
	Node edges[][MAX_EDGES_POS], int *n_edges, int nchars)
{
	Node *tmp  = (Node *)malloc(512 * sizeof(Node));
	Node *best = (Node *)malloc(512 * sizeof(Node));
	if (!tmp || !best) { free(tmp); free(best); return 1; }

	for (int i = 0; i < nchars; i++) {
		n_edges[i] = 0;
		int kind = passthrough_kind_at(original, i);
		if (kind) {
			int end = i + 1;
			while (end < nchars && end - i < 24
				&& passthrough_kind_at(original, end) == kind)
				end++;
			fill_unknown_node(&edges[i][0], yomi, original, i, end, m);
			n_edges[i] = 1;
			continue;
		}

		int nt = 0;
		int lim = d->max_read_chars;
		if (lim > nchars - i) lim = nchars - i;
		if (lim > 24) lim = 24;
		/* longest-first enumeration */
		for (int L = lim; L >= 1; L--) {
			/* Never convert across an explicitly typed Latin/katakana run. */
			if (span_has_passthrough(original, i, L)) continue;
			if (i + L < nchars && is_small_kana(peek_cp_at_char(yomi, i + L)))
				continue;
			char span[MAX_READ_KEY];
			utf8_slice(yomi, i, L, span, sizeof(span));
			if (nt >= 500) break;
			nt += collect_cands(d, m, span, i, i + L, tmp + nt, 512 - nt);
		}
		if (nt == 0) {
			int end = i + 1;
			while (end < nchars && end - i < 24) {
				if (passthrough_kind_at(original, end)) break;
				if (has_dictionary_match(d, yomi, original, end, nchars)) break;
				end++;
			}
			fill_unknown_node(&edges[i][0], yomi, original, i, end, m);
			n_edges[i] = 1;
			continue;
		}
		/* dedupe by (word,end,wclass,typ,form) keep best cost */
		int nb = 0;
		for (int t = 0; t < nt; t++) {
			int found = -1;
			for (int j = 0; j < nb; j++) {
				if (best[j].end == tmp[t].end
					&& best[j].wclass == tmp[t].wclass
					&& best[j].typ == tmp[t].typ
					&& best[j].form == tmp[t].form
					&& strcmp(best[j].word, tmp[t].word) == 0) {
					found = j;
					break;
				}
			}
			if (found < 0) {
				if (nb < 512) best[nb++] = tmp[t];
			} else if (tmp[t].lexical_cost < best[found].lexical_cost) {
				best[found] = tmp[t];
			}
		}
		qsort(best, (size_t)nb, sizeof(Node), node_len_cost_cmp);
		int nlong = 0, nshort = 0;
		for (int j = 0; j < nb && n_edges[i] < MAX_EDGES_POS; j++) {
			if (best[j].read_len >= 2) {
				if (nlong >= 45) continue;
				edges[i][n_edges[i]++] = best[j];
				nlong++;
			}
		}
		for (int j = 0; j < nb && n_edges[i] < MAX_EDGES_POS; j++) {
			if (best[j].read_len == 1) {
				if (nshort >= 10) continue;
				edges[i][n_edges[i]++] = best[j];
				nshort++;
			}
		}
	}
	free(tmp);
	free(best);
	return 0;
}

static float word_cost_f(const Node *e, ConnectNet *m)
{
	float c = e->lexical_cost;
	int L = e->read_len;
	if (L == 1 && !is_particle(e->word) && c < m->uni_floor)
		c = m->uni_floor + c;
	if (L >= 3 && (float)e->cost >= m->rare_cost)
		c += m->rare_long;
	return c;
}

static float length_bonus_f(const Node *e, ConnectNet *m)
{
	int L = e->read_len;
	if (L <= 1) return 0.f;
	return m->mora * (float)L + m->mora2 * (float)(L * L);
}

static float short_penalty_f(
	const Node *e, const Node *edges_at, int n_at, ConnectNet *m)
{
	if (is_particle(e->word)) return 0.f;
	int L = e->read_len;
	float pen = 0.f;
	for (int i = 0; i < n_at; i++) {
		int ol = edges_at[i].read_len;
		if (ol <= L) continue;
		if ((float)edges_at[i].cost >= m->rare_cost) continue;
		if ((float)edges_at[i].cost <= (float)e->cost + m->short_margin)
			pen += m->short_pen * (float)(ol - L);
	}
	if (pen > 4000.f) pen = 4000.f;
	return pen;
}

static float step_cost(
	ConnectNet *m,
	const Node *left, /* NULL = BOS */
	const Node *e,
	const Node *edges_at, int n_at)
{
	int lw, lt, lf, lh;
	if (!left) {
		lw = BOS_WCLASS; lt = -1; lf = -1; lh = 0;
	} else {
		lw = left->wclass; lt = left->typ; lf = left->form; lh = left->hid;
	}
	float cc = connect_cost(
		m, lw, lt, lf, lh,
		e->wclass, e->typ, e->form, e->hid);
	return word_cost_f(e, m) + cc - length_bonus_f(e, m) + m->frag
		+ short_penalty_f(e, edges_at, n_at, m);
}

typedef struct {
	float cost; /* lower is better */
	int n;
	Node nodes[MAX_PATH];
} Beam;

static int beam_cmp_asc(const void *a, const void *b)
{
	float da = ((const Beam *)a)->cost;
	float db = ((const Beam *)b)->cost;
	return (da > db) - (da < db);
}

static int g_verbose = 0;

static int convert(
	WhizDic *dic, ConnectNet *m,
	const char *yomi_in, char *out, int outsz, int beam_size,
	char *segments, int segments_sz)
{
	if (segments && segments_sz > 0) segments[0] = 0;
	char yomi[MAX_YOMI];
	hira_to_kata(yomi_in, yomi, sizeof(yomi));
	int nchars = utf8_nchars(yomi);
	if (nchars <= 0) { out[0] = 0; return 0; }
	if (beam_size > MAX_BEAM) beam_size = MAX_BEAM;
	if (beam_size < 8) beam_size = 8;
	if (nchars >= MAX_YOMI) nchars = MAX_YOMI - 1;

	/* heap tables sized to this yomi (avoid ~170MB static BSS for WASM) */
	Node (*edges)[MAX_EDGES_POS] = calloc((size_t)nchars, sizeof(*edges));
	int *n_edges = calloc((size_t)nchars, sizeof(int));
	Beam (*beams)[MAX_BEAM] = calloc((size_t)nchars + 1, sizeof(*beams));
	int *nb = calloc((size_t)nchars + 1, sizeof(int));
	if (!edges || !n_edges || !beams || !nb) {
		free(edges); free(n_edges); free(beams); free(nb);
		out[0] = 0;
		return 0;
	}
	build_lattice(dic, m, yomi, yomi_in, edges, n_edges, nchars);

	if (g_verbose) {
		fprintf(stderr, "yomi=%s nchars=%d\n", yomi, nchars);
		for (int e = 0; e < n_edges[0] && e < 8; e++) {
			Node *nd = &edges[0][e];
			fprintf(stderr, "  [%d] %s L=%d cost=%d\n",
				e, nd->word, nd->read_len, nd->cost);
		}
	}

	nb[0] = 1;
	beams[0][0].cost = 0.f;
	beams[0][0].n = 0;

	for (int pos = 0; pos < nchars; pos++) {
		if (nb[pos] == 0) continue;
		qsort(beams[pos], (size_t)nb[pos], sizeof(Beam), beam_cmp_asc);
		if (nb[pos] > beam_size) nb[pos] = beam_size;

		/* expand longer candidates first */
		Node ordered[MAX_EDGES_POS];
		int no = n_edges[pos];
		memcpy(ordered, edges[pos], (size_t)no * sizeof(Node));
		qsort(ordered, (size_t)no, sizeof(Node), node_len_cost_cmp);

		for (int bi = 0; bi < nb[pos]; bi++) {
			Beam *b = &beams[pos][bi];
			const Node *left = b->n ? &b->nodes[b->n - 1] : NULL;
			for (int e = 0; e < no; e++) {
				Node *cand = &ordered[e];
				int end = cand->end;
				if (end > nchars) continue;
				if (b->n >= MAX_PATH - 1) break;
				float sc = b->cost + step_cost(m, left, cand, edges[pos], n_edges[pos]);
				if (nb[end] < MAX_BEAM) {
					Beam *d = &beams[end][nb[end]++];
					*d = *b;
					d->nodes[d->n++] = *cand;
					d->cost = sc;
				} else {
					/* replace worst if better */
					int worst = 0;
					for (int k = 1; k < nb[end]; k++)
						if (beams[end][k].cost > beams[end][worst].cost) worst = k;
					if (sc < beams[end][worst].cost) {
						Beam *d = &beams[end][worst];
						*d = *b;
						d->nodes[d->n++] = *cand;
						d->cost = sc;
					}
				}
			}
		}
	}

	if (nb[nchars] == 0) {
		kata_to_hira(yomi, out, outsz);
		free(edges); free(n_edges); free(beams); free(nb);
		return (int)strlen(out);
	}
	/* add EOS and pick best */
	float best_c = 1e30f;
	int best_i = 0;
	for (int i = 0; i < nb[nchars]; i++) {
		Beam *b = &beams[nchars][i];
		float eos = connect_cost(
			m,
			b->nodes[b->n - 1].wclass, b->nodes[b->n - 1].typ,
			b->nodes[b->n - 1].form, b->nodes[b->n - 1].hid,
			EOS_WCLASS, -1, -1, 0);
		float c = b->cost + eos;
		if (c < best_c) { best_c = c; best_i = i; }
	}
	Beam *best = &beams[nchars][best_i];
	out[0] = 0;
	int so = 0;
	for (int i = 0; i < best->n; i++) {
		strncat(out, best->nodes[i].word, outsz - strlen(out) - 1);
		if (segments && so < segments_sz - 1) {
			char hira[MAX_READ_KEY];
			kata_to_hira(best->nodes[i].read, hira, sizeof(hira));
			int n = snprintf(segments + so, (size_t)(segments_sz - so),
			                 "%s\t%s\n", best->nodes[i].word, hira);
			if (n < 0 || n >= segments_sz - so) {
				segments[segments_sz - 1] = 0;
				so = segments_sz - 1;
			} else {
				so += n;
			}
		}
	}
	if (segments && so > 0 && segments[so - 1] == '\n')
		segments[so - 1] = 0;

	if (g_verbose) {
		fprintf(stderr, "best cost=%.1f  ", best_c);
		for (int i = 0; i < best->n; i++) {
			if (i) fputc('/', stderr);
			fputs(best->nodes[i].word, stderr);
		}
		fputc('\n', stderr);
	}
	int ret = (int)strlen(out);
	free(edges); free(n_edges); free(beams); free(nb);
	return ret;
}

// ─── Reverse / Predict ──────────────────────────────────────────────────────

typedef struct { const char *word; const char *read; int cost; } PredCand;

static int pred_cand_cmp(const void *a, const void *b)
{
	const PredCand *x = (const PredCand *)a, *y = (const PredCand *)b;
	return (x->cost > y->cost) - (x->cost < y->cost);
}

static int node_cost_cmp(const void *a, const void *b)
{
	const Node *x = (const Node *)a, *y = (const Node *)b;
	return (x->lexical_cost > y->lexical_cost)
	     - (x->lexical_cost < y->lexical_cost);
}

/* Exact-reading candidates used when cycling a converted segment. */
static int segment_candidates(
	WhizDic *d, ConnectNet *m, const char *reading,
	char *out, int outsz, int max_cands)
{
	if (max_cands <= 0 || max_cands > 256) max_cands = 20;
	char kata[MAX_READ_KEY];
	hira_to_kata(reading, kata, sizeof(kata));
	int chars = utf8_nchars(kata);
	if (chars <= 0) return 0;

	Node nodes[512];
	int count = collect_cands(d, m, kata, 0, chars, nodes, 512);
	qsort(nodes, (size_t)count, sizeof(nodes[0]), node_cost_cmp);
	int o = 0, emitted = 0;
	for (int i = 0; i < count && emitted < max_cands; i++) {
		int duplicate = 0;
		for (int j = 0; j < i; j++) {
			if (!strcmp(nodes[j].word, nodes[i].word)) {
				duplicate = 1;
				break;
			}
		}
		if (duplicate) continue;
		int n = snprintf(out + o, (size_t)(outsz - o), "%s\n", nodes[i].word);
		if (n < 0 || n >= outsz - o) break;
		o += n;
		emitted++;
	}
	if (o > 0) out[--o] = 0;
	else out[0] = 0;
	return o;
}

/*
 * reverse_convert — 漢字テキスト → ひらがな読み
 * word_order で二分探索して最長一致のマッチを繰り返す。
 * 辞書未登録文字はそのまま出力する。
 */
static int reverse_convert(WhizDic *d, const char *text, char *out, int outsz)
{
	int o = 0;
	int nchars = utf8_nchars(text);
	int pos = 0;
	while (pos < nchars && o + 32 < outsz) {
		int lim = nchars - pos;
		if (lim > 24) lim = 24;
		int matched = 0;
		for (int L = lim; L >= 1; L--) {
			char span[MAX_WORD];
			utf8_slice(text, pos, L, span, sizeof(span));
			int lo = 0, hi = d->n;
			while (lo < hi) {
				int mid = (lo + hi) / 2;
				int c = strcmp(d->entries[d->word_order[mid]].word, span);
				if (c < 0) lo = mid + 1; else hi = mid;
			}
			if (lo < d->n && strcmp(d->entries[d->word_order[lo]].word, span) == 0) {
				char hira[MAX_READ_KEY];
				kata_to_hira(d->entries[d->word_order[lo]].read, hira, sizeof(hira));
				int n = (int)strlen(hira);
				if (o + n < outsz) { memcpy(out + o, hira, n); o += n; }
				pos += L;
				matched = 1;
				break;
			}
		}
		if (!matched) {
			char span[8];
			utf8_slice(text, pos, 1, span, sizeof(span));
			int n = (int)strlen(span);
			if (o + n < outsz) { memcpy(out + o, span, n); o += n; }
			pos++;
		}
	}
	out[o] = 0;
	return o;
}

/*
 * predict_convert — 部分的な読み → 候補一覧
 * 出力: "表記\t読み\n" を cost 昇順で並べた文字列（末尾 \n なし）。
 */
static int predict_convert(WhizDic *d, const char *yomi_prefix,
                            char *out, int outsz, int max_cands)
{
	if (max_cands <= 0 || max_cands > 256) max_cands = 20;
	char kata[MAX_READ_KEY];
	hira_to_kata(yomi_prefix, kata, sizeof(kata));
	int plen = (int)strlen(kata);
	if (plen == 0) return 0;

	int lo = 0, hi = d->n;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (strncmp(d->entries[d->order[mid]].read, kata, (size_t)plen) < 0) lo = mid + 1;
		else hi = mid;
	}

	PredCand cands[256];
	int nc = 0;
	for (int i = lo; i < d->n && nc < 256; i++) {
		DicEntry *e = &d->entries[d->order[i]];
		if (strncmp(e->read, kata, (size_t)plen) != 0) break;
		if (bad_surface(e->read, e->word)) continue;
		int dup = 0;
		for (int j = 0; j < nc; j++) {
			if (strcmp(cands[j].word, e->word) == 0) { dup = 1; break; }
		}
		if (!dup) {
			cands[nc].word = e->word;
			cands[nc].read = e->read;
			cands[nc].cost = e->cost;
			nc++;
		}
	}
	qsort(cands, (size_t)nc, sizeof(PredCand), pred_cand_cmp);
	if (nc > max_cands) nc = max_cands;

	int o = 0;
	for (int i = 0; i < nc && o + 256 < outsz; i++) {
		char hira[MAX_READ_KEY];
		kata_to_hira(cands[i].read, hira, sizeof(hira));
		int n = snprintf(out + o, (size_t)(outsz - o), "%s\t%s\n",
		                 cands[i].word, hira);
		if (n > 0) o += n;
	}
	if (o > 0 && out[o - 1] == '\n') out[--o] = 0;
	else out[o] = 0;
	return o;
}

/* ─── Public API (WASM + native library + standalone CLI) ─── */

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WHIZ_API EMSCRIPTEN_KEEPALIVE
#else
#define WHIZ_API
#endif

static ConnectNet g_net;
static WhizDic    g_dic;
static int        g_ready = 0;
static char       g_out[1024];
static char       g_aux_out[4096]; /* reverse / predict output */

WHIZ_API void        whiz_free(void);
WHIZ_API const char *whiz_reverse(const char *word);
WHIZ_API const char *whiz_predict(const char *yomi_prefix, int max_cands);
WHIZ_API const char *whiz_convert_segments(const char *yomi);
WHIZ_API const char *whiz_segment_candidates(const char *reading, int max_cands);

WHIZ_API
int whiz_init(uint8_t *model, int model_sz, uint8_t *dic, int dic_sz)
{
	if (g_ready) whiz_free();
	if (wcnn_load_mem(model, (size_t)model_sz, &g_net) != 0) return 0;
	if (whiz_load_mem(dic, (size_t)dic_sz, &g_dic) != 0) {
		wcnn_free(&g_net);
		return 0;
	}
	if ((uint32_t)g_dic.n != g_net.n_words) {
		fprintf(stderr, "dictionary/model mismatch: %d != %u\n", g_dic.n, g_net.n_words);
		whizdic_free(&g_dic);
		wcnn_free(&g_net);
		return 0;
	}
	g_ready = 1;
	return 1;
}

WHIZ_API
const char *whiz_convert(const char *yomi)
{
	if (!g_ready || !yomi || !yomi[0]) return NULL;
	int n = convert(&g_dic, &g_net, yomi, g_out, (int)sizeof(g_out), 32, NULL, 0);
	return n > 0 ? g_out : NULL;
}

/*
 * whiz_convert_segments — 最良経路を文節へ分けて返す。
 * 出力形式: "表記\t読み" の改行区切り。戻り値は内部バッファ。
 */
WHIZ_API
const char *whiz_convert_segments(const char *yomi)
{
	if (!g_ready || !yomi || !yomi[0]) return NULL;
	int n = convert(&g_dic, &g_net, yomi, g_out, (int)sizeof(g_out), 32,
	                g_aux_out, (int)sizeof(g_aux_out));
	return n > 0 && g_aux_out[0] ? g_aux_out : NULL;
}

WHIZ_API
const char *whiz_segment_candidates(const char *reading, int max_cands)
{
	if (!g_ready || !reading || !reading[0]) return NULL;
	int n = segment_candidates(&g_dic, &g_net, reading, g_aux_out,
	                           (int)sizeof(g_aux_out), max_cands);
	return n > 0 ? g_aux_out : NULL;
}

WHIZ_API int  whiz_is_ready(void) { return g_ready; }

/*
 * whiz_reverse — 漢字テキスト → ひらがな読み
 * 例: "東京" → "とうきょう"
 * 辞書未登録の文字はそのまま返す。戻り値は内部バッファへのポインタ。
 */
WHIZ_API
const char *whiz_reverse(const char *word)
{
	if (!g_ready || !word || !word[0]) return NULL;
	int n = reverse_convert(&g_dic, word, g_aux_out, (int)sizeof(g_aux_out));
	return n > 0 ? g_aux_out : NULL;
}

/*
 * whiz_predict — 部分的な読み → 候補一覧
 * 出力形式: "表記\t読み" を改行区切りで cost 昇順に並べた文字列。
 * max_cands ≤ 0 のとき既定値 20 を使用する。
 */
WHIZ_API
const char *whiz_predict(const char *yomi_prefix, int max_cands)
{
	if (!g_ready || !yomi_prefix || !yomi_prefix[0]) return NULL;
	int n = predict_convert(&g_dic, yomi_prefix, g_aux_out,
	                        (int)sizeof(g_aux_out), max_cands);
	return n > 0 ? g_aux_out : NULL;
}

WHIZ_API
void whiz_free(void)
{
	if (!g_ready) return;
	whizdic_free(&g_dic);
	wcnn_free(&g_net);
	memset(&g_dic, 0, sizeof(g_dic));
	memset(&g_net, 0, sizeof(g_net));
	g_ready = 0;
}

/* Standalone CLI — excluded when compiled as a library */
#if !defined(__EMSCRIPTEN__) && !defined(WHIZ_LIBRARY)
static void usage(const char *cmd)
{
	fprintf(stderr,
		"Usage: %s -m model.wcnn -d whiz.dic -p \"よみ\" [-b beam] [-v]\n"
		"  python3 whiz.py train --preset quick\n"
		"  python3 whiz.py export   # → out/model.wcnn\n",
		cmd);
}

int main(int argc, char **argv)
{
	const char *model_path = NULL;
	const char *dic_path = NULL;
	const char *prompt = NULL;
	int beam = 32;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-m") && i + 1 < argc) model_path = argv[++i];
		else if (!strcmp(argv[i], "-d") && i + 1 < argc) dic_path = argv[++i];
		else if (!strcmp(argv[i], "-p") && i + 1 < argc) prompt = argv[++i];
		else if (!strcmp(argv[i], "-b") && i + 1 < argc) beam = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-v")) g_verbose = 1;
		else { usage(argv[0]); return 1; }
	}
	if (!model_path || !dic_path || !prompt) {
		usage(argv[0]);
		return 1;
	}

	ConnectNet model;
	if (wcnn_load(model_path, &model) != 0) return 1;

	WhizDic dic;
	if (whiz_load(dic_path, &dic) != 0) {
		wcnn_free(&model);
		return 1;
	}
	if ((uint32_t)dic.n != model.n_words) {
		fprintf(stderr, "dictionary/model mismatch: %d != %u\n", dic.n, model.n_words);
		whizdic_free(&dic);
		wcnn_free(&model);
		return 1;
	}

	char out[1024];
	int n = convert(&dic, &model, prompt, out, sizeof(out), beam, NULL, 0);
	if (n > 0) printf("%s\n", out);
	else fprintf(stderr, "empty result\n");

	whizdic_free(&dic);
	wcnn_free(&model);
	return 0;
}
#endif /* !__EMSCRIPTEN__ && !WHIZ_LIBRARY */
