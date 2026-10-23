/*
 * Copyright © 2026 Yuichiro Nakada / Project Whiz
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//---------------------------------------------------------
//	whiz — kana→kanji: longest-match lattice + dictionary costs + LM rescore
//
//	Matches engine/whiz.py:
//	  word cost    whiz.dic entry cost (from WUTF v4)
//	  connect cost connect.dic table   (from WUTF v4 CNNX section)
//	  LM rescore   ContextNet (Transformer) morph-token LM (WCNN v5)
//	               ctx_scale * sum -log P_LM(t_j | t_<j)
//
//	Build:  make
//	Export: python3 whiz.py export-web   → out/model.wcnn + out/whiz.web.dic
//	Run:    ./whiz -m out/model.wcnn -d out/whiz.web.dic -p "とうきょうにいきます"
//---------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define MAX_YOMI       384
#define MAX_BEAM       40
#define MAX_PATH       48
#define MAX_WORD       96
#define MAX_READ_KEY   96
#define MAX_EDGES_POS  64
#define BOS_WCLASS     0
#define EOS_WCLASS     1

#define DEFAULT_CONNECT 2500  /* connect cost when no rule matches */

/* morph LM special token ids (MORPH_SPECIAL in whiz.py) */
#define TOK_PAD 0
#define TOK_UNK 1
#define TOK_BOS 2
#define TOK_EOS 3

/* oov_lex category ids (must stay in sync with whiz.py) */
#define LEX_PAD   0
#define LEX_UNK   1
#define LEX_BOS   2
#define LEX_EOS   3
#define LEX_NUM   4
#define LEX_ALPHA 5
#define LEX_KATA  6
#define LEX_SYM   7

/* Stage 3 costs one softmax over the morph vocab per token, so rescoring is
 * linear in the number of candidates.  This must match whiz.py's `nbest`
 * default: the right answer is regularly ranked below 8th by stage1+stage2
 * alone — that is precisely what stage 3 is there to fix — so a tighter cap
 * silently drops conversions the Python decoder gets right.  Trade latency for
 * it with -c 0, not by narrowing this. */
#define CTX_RESCORE_MAX 32
#define CTX_MAX_TOK     96

typedef struct {
	char *s;
	uint16_t id;
	uint16_t nbytes;
	uint16_t nchars;
} MorphEnt;

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

/* 長音符は平仮名側でも使われる（「きーぼーど」）。Unicode 上は片仮名ブロック
 * だが、これ単独を「利用者が片仮名で入力した」印にすると外来語が変換対象から
 * 外れる。whiz.py の _is_typed_katakana_char と対。 */
static int is_neutral_kana_cp(int cp)
{
	return cp == 0x30FC || cp == 0xFF70;
}

static int passthrough_kind_at(const char *s, int char_idx)
{
	int cp = peek_cp_at_char(s, char_idx);
	if (is_alphabet_cp(cp)) return 1;
	if (is_katakana_cp(cp) && !is_neutral_kana_cp(cp)) return 2;
	return 0;
}

static int span_has_passthrough(const char *s, int start, int nchar)
{
	for (int i = 0; i < nchar; i++)
		if (passthrough_kind_at(s, start + i)) return 1;
	return 0;
}

static int is_digit_cp(int cp)
{
	return (cp >= '0' && cp <= '9') || (cp >= 0xFF10 && cp <= 0xFF19);
}

/* Lexeme id for a surface the model's vocabulary does not cover.
 * Mirrors _oov_lex in whiz.py, including the order of the tests. */
static int oov_lex(const char *word)
{
	if (!word || !word[0]) return LEX_UNK;
	int len = 0;
	int cp = utf8_decode((const unsigned char *)word, &len);
	if (is_digit_cp(cp)) return LEX_NUM;
	if (is_alphabet_cp(cp)) return LEX_ALPHA;
	if (is_katakana_cp(cp)) return LEX_KATA;
	static const char *sym[] = {
		"、", "。", "．", "，", "・", "：", "；", "！", "？",
		"…", "―", "☆", "★", "※", "○", "●", "□", "■",
		"▲", "△", "▽", "▼", "◇", "◆", NULL
	};
	for (int i = 0; sym[i]; i++)
		if (strcmp(word, sym[i]) == 0) return LEX_SYM;
	return LEX_UNK;
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

// ─── model (.wcnn) ──────────────────────────────────────────────────────────

/* One norm_first Transformer encoder layer (stage 3). */
typedef struct {
	float *ln1_g, *ln1_b;
	float *qkv_w, *qkv_b;   /* [3C, C], [3C] — torch packs q,k,v in one matrix */
	float *o_w, *o_b;       /* [C, C], [C] */
	float *ln2_g, *ln2_b;
	float *ff0_w, *ff0_b;   /* [4C, C], [4C] */
	float *ff1_w, *ff1_b;   /* [C, 4C], [C] */
} CtxLayer;

typedef struct {
	/* longest-match hyperparameters (from Weights in whiz.py) */
	float mora, mora2, frag, short_pen, short_margin;
	float rare_cost, rare_long, uni_floor, ctx_scale;

	/* causal Transformer morph-token LM (WCNN v5) */
	uint32_t ctx_dim, ctx_layer, ctx_head, ctx_max_len, n_tok;
	float *ctx_tok, *ctx_posit;
	CtxLayer *layers;
	float *out_ln_g, *out_ln_b;
	MorphEnt *morph_multi;
	MorphEnt *morph_char;
	uint32_t n_morph_multi, n_morph_char, max_multi_len;
	int has_ctx;
} WhizModel;

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

static float half_to_float(uint16_t h)
{
	uint32_t s = (uint32_t)(h & 0x8000) << 16, e = (h >> 10) & 0x1F, m = h & 0x3FF, o;
	if (e == 0) {
		if (!m) o = s;
		else { /* subnormal */
			e = 127 - 14;
			while (!(m & 0x400)) { m <<= 1; e--; }
			o = s | (e << 23) | ((m & 0x3FF) << 13);
		}
	} else if (e == 31) o = s | 0x7F800000u | (m << 13);
	else o = s | ((e + 112) << 23) | (m << 13);
	float f; memcpy(&f, &o, 4);
	return f;
}

/* Weight matrix in WCNN v6: qmode 0 = f32, 1 = f16, 2 = int8 with one f32 scale per row.
 * Always dequantised to f32 at load, so the compute path is unchanged. */
static float *read_mat_mem(const uint8_t **pp, const uint8_t *end, size_t rows,
                           size_t cols, uint32_t qmode)
{
	if (qmode == 0) return read_f32_mem(pp, end, rows * cols);
	size_t n = rows * cols;
	float *out = (float *)malloc(n * sizeof(float));
	if (!out) return NULL;
	if (qmode == 1) {
		if (*pp + n * 2 > end) { free(out); return NULL; }
		for (size_t i = 0; i < n; i++) {
			uint16_t h; memcpy(&h, *pp + i * 2, 2);
			out[i] = half_to_float(h);
		}
		*pp += n * 2;
	} else if (qmode == 2) {
		if (*pp + rows * 4 + n > end) { free(out); return NULL; }
		const uint8_t *sc = *pp, *q = *pp + rows * 4;
		for (size_t r = 0; r < rows; r++) {
			float s; memcpy(&s, sc + r * 4, 4);
			for (size_t k = 0; k < cols; k++)
				out[r * cols + k] = s * (float)(int8_t)q[r * cols + k];
		}
		*pp += rows * 4 + n;
	} else { free(out); return NULL; }
	return out;
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

static int wcnn_load_morph(const uint8_t **pp, const uint8_t *end, WhizModel *m)
{
	const uint8_t *p = *pp;
	if (read_u32_mem(&p, end, &m->n_morph_multi)
		|| read_u32_mem(&p, end, &m->max_multi_len))
		return 1;
	m->morph_multi = (MorphEnt *)calloc(m->n_morph_multi ? m->n_morph_multi : 1, sizeof(MorphEnt));
	for (uint32_t i = 0; i < m->n_morph_multi; i++) {
		uint16_t id = 0, nbytes = 0;
		if (p + 4 > end) return 1;
		memcpy(&id, p, 2); p += 2;
		memcpy(&nbytes, p, 2); p += 2;
		if (p + nbytes > end) return 1;
		m->morph_multi[i].id = id;
		m->morph_multi[i].nbytes = nbytes;
		m->morph_multi[i].s = (char *)malloc((size_t)nbytes + 1);
		if (!m->morph_multi[i].s) return 1;
		memcpy(m->morph_multi[i].s, p, nbytes);
		m->morph_multi[i].s[nbytes] = 0;
		p += nbytes;
		m->morph_multi[i].nchars = (uint16_t)utf8_nchars(m->morph_multi[i].s);
	}
	if (read_u32_mem(&p, end, &m->n_morph_char)) return 1;
	m->morph_char = (MorphEnt *)calloc(m->n_morph_char ? m->n_morph_char : 1, sizeof(MorphEnt));
	for (uint32_t i = 0; i < m->n_morph_char; i++) {
		uint16_t id = 0, nbytes = 0;
		if (p + 4 > end) return 1;
		memcpy(&id, p, 2); p += 2;
		memcpy(&nbytes, p, 2); p += 2;
		if (p + nbytes > end) return 1;
		m->morph_char[i].id = id;
		m->morph_char[i].nbytes = nbytes;
		m->morph_char[i].s = (char *)malloc((size_t)nbytes + 1);
		if (!m->morph_char[i].s) return 1;
		memcpy(m->morph_char[i].s, p, nbytes);
		m->morph_char[i].s[nbytes] = 0;
		p += nbytes;
		m->morph_char[i].nchars = 1;
	}
	*pp = p;
	return 0;
}

static int wcnn_load_mem(const uint8_t *buf, size_t sz, WhizModel *m)
{
	memset(m, 0, sizeof(*m));
	const uint8_t *p = buf, *end = buf + sz;
	if (sz < 8 || memcmp(p, "WCNN", 4) != 0) {
		fprintf(stderr, "bad WCNN magic\n");
		return 1;
	}
	p += 4;
	uint32_t ver = 0;
	if (read_u32_mem(&p, end, &ver) || (ver != 5 && ver != 6)) {
		fprintf(stderr,
			"unsupported WCNN version %u (need 5 or 6);\n"
			"        re-export with `python3 engine/whiz.py export-web`\n", ver);
		return 1;
	}

	uint32_t qmode = 0;
	if (ver == 6 && read_u32_mem(&p, end, &qmode)) return 1;

	/* shape */
	if (read_u32_mem(&p, end, &m->ctx_dim) || read_u32_mem(&p, end, &m->ctx_layer)
		|| read_u32_mem(&p, end, &m->ctx_head) || read_u32_mem(&p, end, &m->ctx_max_len)
		|| read_u32_mem(&p, end, &m->n_tok))
		return 1;

	/* hyperparameters */
	if (read_f32_one_mem(&p, end, &m->ctx_scale)
		|| read_f32_one_mem(&p, end, &m->mora)
		|| read_f32_one_mem(&p, end, &m->mora2)
		|| read_f32_one_mem(&p, end, &m->frag)
		|| read_f32_one_mem(&p, end, &m->short_pen)
		|| read_f32_one_mem(&p, end, &m->short_margin)
		|| read_f32_one_mem(&p, end, &m->rare_cost)
		|| read_f32_one_mem(&p, end, &m->rare_long)
		|| read_f32_one_mem(&p, end, &m->uni_floor))
		return 1;

	uint32_t C = m->ctx_dim;
	if (C == 0 || m->ctx_head == 0 || C % m->ctx_head != 0 || m->n_tok == 0) {
		fprintf(stderr, "WCNN: bad LM shape dim=%u head=%u n_tok=%u\n",
			C, m->ctx_head, m->n_tok);
		return 1;
	}
	m->ctx_tok   = read_mat_mem(&p, end, m->n_tok, C, qmode);
	m->ctx_posit = read_mat_mem(&p, end, m->ctx_max_len, C, qmode);
	m->layers = (CtxLayer *)calloc(m->ctx_layer, sizeof(CtxLayer));
	if (!m->ctx_tok || !m->ctx_posit || !m->layers) {
		fprintf(stderr, "WCNN truncated (LM embeddings)\n");
		return 1;
	}
	for (uint32_t L = 0; L < m->ctx_layer; L++) {
		CtxLayer *cl = &m->layers[L];
		cl->ln1_g = read_f32_mem(&p, end, C);
		cl->ln1_b = read_f32_mem(&p, end, C);
		cl->qkv_w = read_mat_mem(&p, end, 3 * (size_t)C, C, qmode);
		cl->qkv_b = read_f32_mem(&p, end, (size_t)3 * C);
		cl->o_w   = read_mat_mem(&p, end, C, C, qmode);
		cl->o_b   = read_f32_mem(&p, end, C);
		cl->ln2_g = read_f32_mem(&p, end, C);
		cl->ln2_b = read_f32_mem(&p, end, C);
		cl->ff0_w = read_mat_mem(&p, end, 4 * (size_t)C, C, qmode);
		cl->ff0_b = read_f32_mem(&p, end, (size_t)4 * C);
		cl->ff1_w = read_mat_mem(&p, end, C, 4 * (size_t)C, qmode);
		cl->ff1_b = read_f32_mem(&p, end, C);
		if (!cl->ff1_b) {
			fprintf(stderr, "WCNN truncated (LM layer %u)\n", L);
			return 1;
		}
	}
	m->out_ln_g = read_f32_mem(&p, end, C);
	m->out_ln_b = read_f32_mem(&p, end, C);
	if (!m->out_ln_b) {
		fprintf(stderr, "WCNN truncated (LM output norm)\n");
		return 1;
	}
	if (wcnn_load_morph(&p, end, m)) return 1;

	m->has_ctx = 1;
	fprintf(stderr,
		"model: LM dim=%u x%u heads=%u tok=%u multi=%u\n",
		m->ctx_dim, m->ctx_layer, m->ctx_head, m->n_tok, m->n_morph_multi);
	return 0;
}

#if !defined(__EMSCRIPTEN__) && !defined(WHIZ_LIBRARY)
static int wcnn_load(const char *path, WhizModel *m)
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

static void wcnn_free(WhizModel *m)
{
	if (!m) return;
	free(m->ctx_tok); free(m->ctx_posit);
	for (uint32_t L = 0; m->layers && L < m->ctx_layer; L++) {
		CtxLayer *cl = &m->layers[L];
		free(cl->ln1_g); free(cl->ln1_b);
		free(cl->qkv_w); free(cl->qkv_b);
		free(cl->o_w); free(cl->o_b);
		free(cl->ln2_g); free(cl->ln2_b);
		free(cl->ff0_w); free(cl->ff0_b);
		free(cl->ff1_w); free(cl->ff1_b);
	}
	free(m->layers);
	free(m->out_ln_g); free(m->out_ln_b);
	for (uint32_t i = 0; m->morph_multi && i < m->n_morph_multi; i++)
		free(m->morph_multi[i].s);
	free(m->morph_multi);
	for (uint32_t i = 0; m->morph_char && i < m->n_morph_char; i++)
		free(m->morph_char[i].s);
	free(m->morph_char);
	memset(m, 0, sizeof(*m));
}

static float gelu_f(float x)
{
	/* match torch.nn.GELU (erf) */
	return 0.5f * x * (1.f + erff(x * 0.7071067811865475f));
}

/* ─── connect.dic table (from WUTF v4 CNNX section) ──────────────────────── */

typedef struct {
	int16_t lw, rw;
	int8_t lt, lf, rt, rf;
	uint8_t spec, _pad;
	uint16_t lword_idx, rword_idx;
	int16_t cost, _pad2;
} CnnxRule;

typedef struct {
	int16_t lw, rw;
	int32_t start, count;
} CnnxPair;

typedef struct {
	char   **strings;
	int      n_strings;
	CnnxRule *rules;
	int      n_rules;
	CnnxPair *pairs;
	int      n_pairs;
} ConnectDic;

static int connect_find_pair(const ConnectDic *cd, int lw, int rw)
{
	int lo = 0, hi = cd->n_pairs;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		CnnxPair *p = &cd->pairs[mid];
		if (p->lw < lw || (p->lw == lw && p->rw < rw)) lo = mid + 1;
		else if (p->lw == lw && p->rw == rw) return mid;
		else hi = mid;
	}
	return -1;
}

static int cnnx_side_ok(
	const ConnectDic *cd, int8_t rt, int8_t rf, uint16_t ridx,
	int gt, int gf, const char *gword, const char *gread,
	int loose_t, int loose_f)
{
	if (!loose_t && rt != -1 && rt != (int8_t)gt) return 0;
	if (!loose_f && rf != -1 && rf != (int8_t)gf) return 0;
	if (ridx != 0) {  /* 0 = '*' wildcard */
		const char *rw = cd->strings[ridx];
		if (strcmp(rw, gword) != 0 && strcmp(rw, gread) != 0) return 0;
	}
	return 1;
}

static float connect_cost_table(
	const ConnectDic *cd,
	int lw, int lt, int lf, const char *lword, const char *lread,
	int rw, int rt, int rf, const char *rword, const char *rread)
{
	if (!cd || !cd->n_pairs) return DEFAULT_CONNECT;
	int pi = connect_find_pair(cd, lw, rw);
	if (pi < 0) return DEFAULT_CONNECT;
	int start = cd->pairs[pi].start;
	int count = cd->pairs[pi].count;
	/* 3-pass loosening matching ConnectDic._cost_uncached in whiz.py */
	static const int passes[3][2] = {{0,0},{0,1},{1,1}};
	for (int pass = 0; pass < 3; pass++) {
		int loose_t = passes[pass][0], loose_f = passes[pass][1];
		int best = -1, best_sp = -1;
		for (int i = start; i < start + count; i++) {
			const CnnxRule *r = &cd->rules[i];
			if (!cnnx_side_ok(cd, r->lt, r->lf, r->lword_idx,
				lt, lf, lword, lread, loose_t, loose_f)) continue;
			if (!cnnx_side_ok(cd, r->rt, r->rf, r->rword_idx,
				rt, rf, rword, rread, loose_t, loose_f)) continue;
			int sp = (int)(uint8_t)r->spec;
			if (best < 0 || sp > best_sp || (sp == best_sp && r->cost < best)) {
				best = (int)r->cost;
				best_sp = sp;
			}
		}
		if (best >= 0) return (float)best;
	}
	return DEFAULT_CONNECT;
}

// ─── stage 3: causal Transformer word LM ────────────────────────────────────

static void layer_norm(
	const float *x, const float *g, const float *b, uint32_t n, float *out)
{
	float mean = 0.f;
	for (uint32_t i = 0; i < n; i++) mean += x[i];
	mean /= (float)n;
	float var = 0.f;
	for (uint32_t i = 0; i < n; i++) {
		float d = x[i] - mean;
		var += d * d;
	}
	var /= (float)n;
	float inv = 1.f / sqrtf(var + 1e-5f);
	for (uint32_t i = 0; i < n; i++) out[i] = (x[i] - mean) * inv * g[i] + b[i];
}

/* out[j] = bias[j] + sum_i W[j][i] * x[i]   (torch Linear layout) */
/* Dot product.  The portable version uses 8 independent accumulators so a
 * vectoriser can use SIMD without -ffast-math; on x86-64 an AVX2+FMA version is
 * picked at run time, so a size-optimised (-Os) build is still fast. */
__attribute__((unused)) static float dot_generic(const float *a, const float *b, uint32_t n)
{
	float s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, s5 = 0, s6 = 0, s7 = 0;
	uint32_t i = 0;
	for (; i + 8 <= n; i += 8) {
		s0 += a[i] * b[i];     s1 += a[i + 1] * b[i + 1];
		s2 += a[i + 2] * b[i + 2]; s3 += a[i + 3] * b[i + 3];
		s4 += a[i + 4] * b[i + 4]; s5 += a[i + 5] * b[i + 5];
		s6 += a[i + 6] * b[i + 6]; s7 += a[i + 7] * b[i + 7];
	}
	float s = ((s0 + s4) + (s1 + s5)) + ((s2 + s6) + (s3 + s7));
	for (; i < n; i++) s += a[i] * b[i];
	return s;
}

#if defined(__x86_64__) && defined(__GNUC__) && !defined(__EMSCRIPTEN__)
#include <immintrin.h>
__attribute__((target("avx2,fma")))
static float dot_avx2(const float *a, const float *b, uint32_t n)
{
	__m256 s0 = _mm256_setzero_ps(), s1 = _mm256_setzero_ps();
	uint32_t i = 0;
	for (; i + 16 <= n; i += 16) {
		s0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), s0);
		s1 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 8), _mm256_loadu_ps(b + i + 8), s1);
	}
	for (; i + 8 <= n; i += 8)
		s0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), s0);
	s0 = _mm256_add_ps(s0, s1);
	__m128 lo = _mm256_castps256_ps128(s0), hi = _mm256_extractf128_ps(s0, 1);
	lo = _mm_add_ps(lo, hi);
	lo = _mm_add_ps(lo, _mm_movehl_ps(lo, lo));
	lo = _mm_add_ss(lo, _mm_shuffle_ps(lo, lo, 1));
	float s = _mm_cvtss_f32(lo);
	for (; i < n; i++) s += a[i] * b[i];
	return s;
}
__attribute__((target("avx")))
static float dot_avx(const float *a, const float *b, uint32_t n)
{
	__m256 s0 = _mm256_setzero_ps(), s1 = _mm256_setzero_ps();
	uint32_t i = 0;
	for (; i + 16 <= n; i += 16) {
		s0 = _mm256_add_ps(s0, _mm256_mul_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i)));
		s1 = _mm256_add_ps(s1, _mm256_mul_ps(_mm256_loadu_ps(a + i + 8), _mm256_loadu_ps(b + i + 8)));
	}
	for (; i + 8 <= n; i += 8)
		s0 = _mm256_add_ps(s0, _mm256_mul_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i)));
	s0 = _mm256_add_ps(s0, s1);
	__m128 lo = _mm256_castps256_ps128(s0), hi = _mm256_extractf128_ps(s0, 1);
	lo = _mm_add_ps(lo, hi);
	lo = _mm_add_ps(lo, _mm_movehl_ps(lo, lo));
	lo = _mm_add_ss(lo, _mm_shuffle_ps(lo, lo, 1));
	float s = _mm_cvtss_f32(lo);
	for (; i < n; i++) s += a[i] * b[i];
	return s;
}

/* x86-64 baseline (SSE2) */
static float dot_sse(const float *a, const float *b, uint32_t n)
{
	__m128 s0 = _mm_setzero_ps(), s1 = _mm_setzero_ps();
	uint32_t i = 0;
	for (; i + 8 <= n; i += 8) {
		s0 = _mm_add_ps(s0, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
		s1 = _mm_add_ps(s1, _mm_mul_ps(_mm_loadu_ps(a + i + 4), _mm_loadu_ps(b + i + 4)));
	}
	s0 = _mm_add_ps(s0, s1);
	s0 = _mm_add_ps(s0, _mm_movehl_ps(s0, s0));
	s0 = _mm_add_ss(s0, _mm_shuffle_ps(s0, s0, 1));
	float s = _mm_cvtss_f32(s0);
	for (; i < n; i++) s += a[i] * b[i];
	return s;
}

static int g_dot_kind = -1;   /* 0 = SSE2, 1 = AVX, 2 = AVX2+FMA */
static inline float dot_f(const float *a, const float *b, uint32_t n)
{
	if (g_dot_kind < 0)
		g_dot_kind = (__builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma")) ? 2
		           : __builtin_cpu_supports("avx") ? 1 : 0;
	return g_dot_kind == 2 ? dot_avx2(a, b, n)
	     : g_dot_kind == 1 ? dot_avx(a, b, n) : dot_sse(a, b, n);
}
/* out[v] = dot(h, W[v]) for v < V, four rows per pass so `h` is loaded once per four rows.
 * Each row accumulates exactly as dot_avx / dot_avx2 do, so the values are bit-identical
 * to calling dot_f row by row — only the loads and call overhead are saved. */
#define HEAD4_BODY(MADD) \
	uint32_t v = 0; \
	for (; v + 4 <= V; v += 4) { \
		const float *w0 = W + (size_t)v * C, *w1 = w0 + C, *w2 = w1 + C, *w3 = w2 + C; \
		__m256 a0 = _mm256_setzero_ps(), b0 = a0, a1 = a0, b1 = a0, \
		       a2 = a0, b2 = a0, a3 = a0, b3 = a0; \
		uint32_t i = 0; \
		for (; i + 16 <= C; i += 16) { \
			__m256 x0 = _mm256_loadu_ps(h + i), x1 = _mm256_loadu_ps(h + i + 8); \
			a0 = MADD(x0, _mm256_loadu_ps(w0 + i), a0); b0 = MADD(x1, _mm256_loadu_ps(w0 + i + 8), b0); \
			a1 = MADD(x0, _mm256_loadu_ps(w1 + i), a1); b1 = MADD(x1, _mm256_loadu_ps(w1 + i + 8), b1); \
			a2 = MADD(x0, _mm256_loadu_ps(w2 + i), a2); b2 = MADD(x1, _mm256_loadu_ps(w2 + i + 8), b2); \
			a3 = MADD(x0, _mm256_loadu_ps(w3 + i), a3); b3 = MADD(x1, _mm256_loadu_ps(w3 + i + 8), b3); \
		} \
		for (; i + 8 <= C; i += 8) { \
			__m256 x0 = _mm256_loadu_ps(h + i); \
			a0 = MADD(x0, _mm256_loadu_ps(w0 + i), a0); a1 = MADD(x0, _mm256_loadu_ps(w1 + i), a1); \
			a2 = MADD(x0, _mm256_loadu_ps(w2 + i), a2); a3 = MADD(x0, _mm256_loadu_ps(w3 + i), a3); \
		} \
		float r[4]; \
		__m256 acc[4] = {_mm256_add_ps(a0, b0), _mm256_add_ps(a1, b1), \
		                 _mm256_add_ps(a2, b2), _mm256_add_ps(a3, b3)}; \
		for (int k = 0; k < 4; k++) { \
			__m128 lo = _mm256_castps256_ps128(acc[k]), hi = _mm256_extractf128_ps(acc[k], 1); \
			lo = _mm_add_ps(lo, hi); \
			lo = _mm_add_ps(lo, _mm_movehl_ps(lo, lo)); \
			lo = _mm_add_ss(lo, _mm_shuffle_ps(lo, lo, 1)); \
			r[k] = _mm_cvtss_f32(lo); \
		} \
		for (; i < C; i++) { \
			r[0] += h[i] * w0[i]; r[1] += h[i] * w1[i]; \
			r[2] += h[i] * w2[i]; r[3] += h[i] * w3[i]; \
		} \
		out[v] = r[0]; out[v + 1] = r[1]; out[v + 2] = r[2]; out[v + 3] = r[3]; \
	} \
	for (; v < V; v++) out[v] = DOT(h, W + (size_t)v * C, C);

#define MADD_AVX(x, w, acc) _mm256_add_ps(acc, _mm256_mul_ps(x, w))
#define MADD_FMA(x, w, acc) _mm256_fmadd_ps(x, w, acc)
__attribute__((target("avx2,fma")))
static void head_logits_avx2(const float *h, const float *W, uint32_t V, uint32_t C, float *out)
{
#define DOT dot_avx2
	HEAD4_BODY(MADD_FMA)
#undef DOT
}
__attribute__((target("avx")))
static void head_logits_avx(const float *h, const float *W, uint32_t V, uint32_t C, float *out)
{
#define DOT dot_avx
	HEAD4_BODY(MADD_AVX)
#undef DOT
}

static void head_logits(const float *h, const float *W, uint32_t V, uint32_t C, float *out)
{
	(void)dot_f(h, h, 0);                    /* make sure g_dot_kind is resolved */
	if (g_dot_kind == 2) head_logits_avx2(h, W, V, C, out);
	else if (g_dot_kind == 1) head_logits_avx(h, W, V, C, out);
	else for (uint32_t v = 0; v < V; v++) out[v] = dot_f(h, W + (size_t)v * C, C);
}
/* out[i][j] = bias[j] + dot(X[i], W[j]) for n inputs: two weight rows × two inputs per pass,
 * so each weight row is read once per pair of tokens.  Accumulation order matches
 * dot_avx / dot_avx2 exactly, so results are bit-identical to calling `linear` per token. */
#define HSUM8(v, out) do { \
		__m128 lo_ = _mm256_castps256_ps128(v), hi_ = _mm256_extractf128_ps(v, 1); \
		lo_ = _mm_add_ps(lo_, hi_); \
		lo_ = _mm_add_ps(lo_, _mm_movehl_ps(lo_, lo_)); \
		lo_ = _mm_add_ss(lo_, _mm_shuffle_ps(lo_, lo_, 1)); \
		(out) = _mm_cvtss_f32(lo_); } while (0)
#define LINEAR_N_BODY(MADD, DOT) \
	uint32_t i = 0; \
	for (; i + 2 <= n; i += 2) { \
		const float *x0 = X + (size_t)i * in_n, *x1 = x0 + in_n; \
		float *o0 = out + (size_t)i * out_n, *o1 = o0 + out_n; \
		uint32_t j = 0; \
		for (; j + 2 <= out_n; j += 2) { \
			const float *w0 = W + (size_t)j * in_n, *w1 = w0 + in_n; \
			__m256 z = _mm256_setzero_ps(); \
			__m256 a00 = z, b00 = z, a01 = z, b01 = z, a10 = z, b10 = z, a11 = z, b11 = z; \
			uint32_t k = 0; \
			for (; k + 16 <= in_n; k += 16) { \
				__m256 xa0 = _mm256_loadu_ps(x0 + k), xb0 = _mm256_loadu_ps(x0 + k + 8); \
				__m256 xa1 = _mm256_loadu_ps(x1 + k), xb1 = _mm256_loadu_ps(x1 + k + 8); \
				__m256 wa0 = _mm256_loadu_ps(w0 + k), wb0 = _mm256_loadu_ps(w0 + k + 8); \
				__m256 wa1 = _mm256_loadu_ps(w1 + k), wb1 = _mm256_loadu_ps(w1 + k + 8); \
				a00 = MADD(xa0, wa0, a00); b00 = MADD(xb0, wb0, b00); \
				a01 = MADD(xa0, wa1, a01); b01 = MADD(xb0, wb1, b01); \
				a10 = MADD(xa1, wa0, a10); b10 = MADD(xb1, wb0, b10); \
				a11 = MADD(xa1, wa1, a11); b11 = MADD(xb1, wb1, b11); \
			} \
			for (; k + 8 <= in_n; k += 8) { \
				__m256 xa0 = _mm256_loadu_ps(x0 + k), xa1 = _mm256_loadu_ps(x1 + k); \
				__m256 wa0 = _mm256_loadu_ps(w0 + k), wa1 = _mm256_loadu_ps(w1 + k); \
				a00 = MADD(xa0, wa0, a00); a01 = MADD(xa0, wa1, a01); \
				a10 = MADD(xa1, wa0, a10); a11 = MADD(xa1, wa1, a11); \
			} \
			__m256 t00 = _mm256_add_ps(a00, b00), t01 = _mm256_add_ps(a01, b01); \
			__m256 t10 = _mm256_add_ps(a10, b10), t11 = _mm256_add_ps(a11, b11); \
			float s00, s01, s10, s11; \
			HSUM8(t00, s00); HSUM8(t01, s01); HSUM8(t10, s10); HSUM8(t11, s11); \
			for (; k < in_n; k++) { \
				s00 += x0[k] * w0[k]; s01 += x0[k] * w1[k]; \
				s10 += x1[k] * w0[k]; s11 += x1[k] * w1[k]; \
			} \
			float b0 = bias ? bias[j] : 0.f, b1 = bias ? bias[j + 1] : 0.f; \
			o0[j] = b0 + s00; o0[j + 1] = b1 + s01; \
			o1[j] = b0 + s10; o1[j + 1] = b1 + s11; \
		} \
		for (; j < out_n; j++) { \
			float b0 = bias ? bias[j] : 0.f; \
			o0[j] = b0 + DOT(W + (size_t)j * in_n, x0, in_n); \
			o1[j] = b0 + DOT(W + (size_t)j * in_n, x1, in_n); \
		} \
	} \
	for (; i < n; i++) { \
		float *oi = out + (size_t)i * out_n; \
		HEADROWS(X + (size_t)i * in_n, W, out_n, in_n, oi); \
		if (bias) for (uint32_t j = 0; j < out_n; j++) oi[j] = bias[j] + oi[j]; \
		else for (uint32_t j = 0; j < out_n; j++) oi[j] = 0.f + oi[j]; \
	}

__attribute__((target("avx2,fma")))
static void linear_n_avx2(const float *W, const float *bias, const float *X, uint32_t n,
                          uint32_t out_n, uint32_t in_n, float *out)
{
	#define HEADROWS head_logits_avx2
	LINEAR_N_BODY(MADD_FMA, dot_avx2)
#undef HEADROWS
}
__attribute__((target("avx")))
static void linear_n_avx(const float *W, const float *bias, const float *X, uint32_t n,
                         uint32_t out_n, uint32_t in_n, float *out)
{
	#define HEADROWS head_logits_avx
	LINEAR_N_BODY(MADD_AVX, dot_avx)
#undef HEADROWS
}

static void linear_n(const float *W, const float *bias, const float *X, uint32_t n,
                     uint32_t out_n, uint32_t in_n, float *out)
{
	(void)dot_f(W, W, 0);
	if (g_dot_kind == 2) { linear_n_avx2(W, bias, X, n, out_n, in_n, out); return; }
	if (g_dot_kind == 1) { linear_n_avx(W, bias, X, n, out_n, in_n, out); return; }
	for (uint32_t i = 0; i < n; i++)
		for (uint32_t j = 0; j < out_n; j++)
			out[(size_t)i * out_n + j] = (bias ? bias[j] : 0.f)
				+ dot_f(W + (size_t)j * in_n, X + (size_t)i * in_n, in_n);
}
#else
static inline float dot_f(const float *a, const float *b, uint32_t n)
{
	return dot_generic(a, b, n);
}
static void head_logits(const float *h, const float *W, uint32_t V, uint32_t C, float *out)
{
	for (uint32_t v = 0; v < V; v++) out[v] = dot_f(h, W + (size_t)v * C, C);
}
static void linear_n(const float *W, const float *bias, const float *X, uint32_t n,
                     uint32_t out_n, uint32_t in_n, float *out)
{
	for (uint32_t i = 0; i < n; i++)
		for (uint32_t j = 0; j < out_n; j++)
			out[(size_t)i * out_n + j] = (bias ? bias[j] : 0.f)
				+ dot_f(W + (size_t)j * in_n, X + (size_t)i * in_n, in_n);
}
#endif

static void linear(
	const float *W, const float *bias, const float *x,
	uint32_t out_n, uint32_t in_n, float *out)
{
	for (uint32_t j = 0; j < out_n; j++) {
		out[j] = (bias ? bias[j] : 0.f) + dot_f(W + (size_t)j * in_n, x, in_n);
	}
}

/* Causal self-attention over `t` tokens, in place on `h` ([t, C]).
 * `scratch` needs 4*t*C + t floats: [t,3C] qkv, [t,C] accumulator, [t] weights. */
static void ctx_attention(
	WhizModel *m, CtxLayer *cl, float *h, uint32_t t, float *scratch)
{
	uint32_t C = m->ctx_dim, nh = m->ctx_head, hd = C / nh;
	float *qkv = scratch;              /* [t, 3C] */
	float *acc = qkv + (size_t)t * 3 * C;  /* [t, C] */
	float *att = acc + (size_t)t * C;      /* [t] */
	float inv_sqrt = 1.f / sqrtf((float)hd);

	for (uint32_t i = 0; i < t; i++)
		linear(cl->qkv_w, cl->qkv_b, h + (size_t)i * C, 3 * C, C,
		       qkv + (size_t)i * 3 * C);

	for (uint32_t i = 0; i < t; i++)
		for (uint32_t c = 0; c < C; c++) acc[(size_t)i * C + c] = 0.f;

	for (uint32_t hh = 0; hh < nh; hh++) {
		uint32_t off = hh * hd;
		for (uint32_t i = 0; i < t; i++) {
			const float *q = qkv + (size_t)i * 3 * C + off;
			/* causal: only attend to j <= i */
			float mx = -1e30f;
			for (uint32_t j = 0; j <= i; j++) {
				const float *k = qkv + (size_t)j * 3 * C + C + off;
				float s = 0.f;
				for (uint32_t d = 0; d < hd; d++) s += q[d] * k[d];
				s *= inv_sqrt;
				att[j] = s;
				if (s > mx) mx = s;
			}
			float sum = 0.f;
			for (uint32_t j = 0; j <= i; j++) {
				att[j] = expf(att[j] - mx);
				sum += att[j];
			}
			float inv = 1.f / sum;
			float *o = acc + (size_t)i * C + off;
			for (uint32_t j = 0; j <= i; j++) {
				float wgt = att[j] * inv;
				const float *v = qkv + (size_t)j * 3 * C + 2 * C + off;
				for (uint32_t d = 0; d < hd; d++) o[d] += wgt * v[d];
			}
		}
	}
	for (uint32_t i = 0; i < t; i++)
		linear(cl->o_w, cl->o_b, acc + (size_t)i * C, C, C, h + (size_t)i * C);
}

/* Run the encoder stack over `t` morph tokens and write post-LN states to `h`. */
static int ctx_hidden(WhizModel *m, const int *tok, uint32_t t, float *h)
{
	uint32_t C = m->ctx_dim;
	float *tmp = (float *)malloc((size_t)t * C * sizeof(float));
	float *ff = (float *)malloc((size_t)4 * C * sizeof(float));
	float *scratch = (float *)malloc(((size_t)t * 4 * C + t) * sizeof(float));
	if (!tmp || !ff || !scratch) { free(tmp); free(ff); free(scratch); return 1; }

	for (uint32_t i = 0; i < t; i++) {
		int it = tok[i];
		if (it < 0 || (uint32_t)it >= m->n_tok) it = TOK_UNK;
		uint32_t ipos = i < m->ctx_max_len ? i : m->ctx_max_len - 1;
		float *row = h + (size_t)i * C;
		for (uint32_t c = 0; c < C; c++)
			row[c] = m->ctx_tok[(size_t)it * C + c]
			       + m->ctx_posit[(size_t)ipos * C + c];
	}

	for (uint32_t L = 0; L < m->ctx_layer; L++) {
		CtxLayer *cl = &m->layers[L];
		for (uint32_t i = 0; i < t; i++)
			layer_norm(h + (size_t)i * C, cl->ln1_g, cl->ln1_b, C,
			           tmp + (size_t)i * C);
		ctx_attention(m, cl, tmp, t, scratch);
		for (uint32_t i = 0; i < t * C; i++) h[i] += tmp[i];
		for (uint32_t i = 0; i < t; i++) {
			float *row = h + (size_t)i * C;
			layer_norm(row, cl->ln2_g, cl->ln2_b, C, tmp + (size_t)i * C);
			linear(cl->ff0_w, cl->ff0_b, tmp + (size_t)i * C, 4 * C, C, ff);
			for (uint32_t j = 0; j < 4 * C; j++) ff[j] = gelu_f(ff[j]);
			linear(cl->ff1_w, cl->ff1_b, ff, C, 4 * C, tmp + (size_t)i * C);
			for (uint32_t c = 0; c < C; c++) row[c] += tmp[(size_t)i * C + c];
		}
	}
	for (uint32_t i = 0; i < t; i++) {
		layer_norm(h + (size_t)i * C, m->out_ln_g, m->out_ln_b, C,
		           tmp + (size_t)i * C);
		memcpy(h + (size_t)i * C, tmp + (size_t)i * C, C * sizeof(float));
	}
	free(tmp); free(ff); free(scratch);
	return 0;
}

/* Longest-match morph tokenize (matches MorphVocab.tokenize in whiz.py). */
static int morph_tokenize(WhizModel *m, const char *text, int *out, int out_max)
{
	const unsigned char *p = (const unsigned char *)text;
	int n = 0;
	while (*p && n < out_max) {
		int hit = -1, hit_bytes = 0;
		int remain_chars = utf8_nchars((const char *)p);
		int lim = (int)m->max_multi_len;
		if (lim > remain_chars) lim = remain_chars;
		for (int L = lim; L >= 2; L--) {
			char span[MAX_WORD];
			utf8_slice((const char *)p, 0, L, span, sizeof(span));
			for (uint32_t i = 0; i < m->n_morph_multi; i++) {
				MorphEnt *e = &m->morph_multi[i];
				if ((int)e->nchars != L) continue;
				if (strcmp(e->s, span) == 0) {
					hit = e->id;
					hit_bytes = e->nbytes;
					break;
				}
			}
			if (hit >= 0) break;
		}
		if (hit >= 0) {
			out[n++] = hit;
			p += hit_bytes;
			continue;
		}
		int clen = 0;
		utf8_decode(p, &clen);
		char one[8];
		if (clen <= 0 || clen >= (int)sizeof(one)) { out[n++] = TOK_UNK; break; }
		memcpy(one, p, (size_t)clen);
		one[clen] = 0;
		int id = TOK_UNK;
		for (uint32_t i = 0; i < m->n_morph_char; i++) {
			if (m->morph_char[i].nbytes == (uint16_t)clen
				&& memcmp(m->morph_char[i].s, one, (size_t)clen) == 0) {
				id = m->morph_char[i].id;
				break;
			}
		}
		out[n++] = id;
		p += clen;
	}
	return n;
}

/* ctx_scale * sum -log P(target_i | prefix) over a morph-token path.
 * `tok` is BOS t1..tn, `target` is t1..tn EOS.
 *
 * Keep TOK_UNK in `tok`: later tokens can still use that context.  Do not,
 * however, charge the probability of TOK_UNK as a target.  Older checkpoints
 * did not reserve every katakana character (notably voiced/small kana), and an
 * OOV target carries no information about which dictionary spelling is right.
 * Scoring it made valid loanwords such as キーボード and メニュー lose for an
 * accidental property of the checkpoint vocabulary.  This mirrors the target
 * mask in whiz.py's _context_batch_tail. */
/* Letters of `word` that the LM cannot score; see UNK_FOREIGN_NLL. */
static int foreign_char_count(const WhizModel *m, const char *word)
{
	int n = 0;
	for (const unsigned char *p = (const unsigned char *)word; *p; ) {
		int len = 0;
		int cp = utf8_decode(p, &len);
		if (len <= 0) break;
		if ((cp >= 0xFF21 && cp <= 0xFF3A) || (cp >= 0xFF41 && cp <= 0xFF5A) ||
		    (cp >= 0x0370 && cp <= 0x04FF)) {
			int known = 0;
			for (uint32_t i = 0; i < m->n_morph_char && !known; i++)
				known = m->morph_char[i].nbytes == (uint16_t)len
				     && memcmp(m->morph_char[i].s, p, (size_t)len) == 0;
			if (!known) n++;
		}
		p += len;
	}
	return n;
}

static int tok_seq_cmp(const int *a, int na, const int *b, int nb)
{
	int n = na < nb ? na : nb;
	for (int i = 0; i < n; i++)
		if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
	return na - nb;
}

static float ctx_path_cost(
	WhizModel *m, const int *tok, const int *target, uint32_t t)
{
	if (!m->has_ctx || t == 0) return 0.f;
	if (t > CTX_MAX_TOK) t = CTX_MAX_TOK;
	uint32_t C = m->ctx_dim, V = m->n_tok;
	int has_scored_target = 0;
	for (uint32_t i = 0; i < t; i++) {
		int tgt = target[i];
		if (tgt < 0 || (uint32_t)tgt >= V) tgt = TOK_UNK;
		if (tgt != TOK_UNK) { has_scored_target = 1; break; }
	}
	if (!has_scored_target) return 0.f;
	float *h = (float *)malloc((size_t)t * C * sizeof(float));
	float *logits = (float *)malloc((size_t)V * sizeof(float));
	if (!h || !logits) { free(h); free(logits); return 0.f; }
	if (ctx_hidden(m, tok, t, h)) { free(h); free(logits); return 0.f; }

	double nll = 0.0;
	for (uint32_t i = 0; i < t; i++) {
		int tgt = target[i];
		if (tgt < 0 || (uint32_t)tgt >= V) tgt = TOK_UNK;
		if (tgt == TOK_UNK) continue;

		const float *hi = h + (size_t)i * C;
		float mx = -1e30f;
		for (uint32_t v = 0; v < V; v++) {
			const float *e = m->ctx_tok + (size_t)v * C;
			float s = dot_f(hi, e, C);
			logits[v] = s;
			if (s > mx) mx = s;
		}
		double sum = 0.0;
		for (uint32_t v = 0; v < V; v++) sum += expf(logits[v] - mx);
		nll -= (double)(logits[tgt] - mx) - log(sum);
	}
	free(h); free(logits);
	return m->ctx_scale * (float)nll;
}


/* ─── incremental LM scoring with a shared-prefix cache ───────────────────────
 * The n-best paths of one conversion differ in a few words, so most of their
 * token sequences are identical prefixes.  The model is causal: hidden states
 * (and the per-position nll) of a shared prefix are identical, so they are kept
 * and only the differing suffix is recomputed.  Paths are scored in
 * lexicographic token order so neighbours share the longest prefixes. */
typedef struct {
	int n;                      /* tokens held (BOS t1..tn) */
	int tok[CTX_MAX_TOK];
	float *K, *V;               /* [layer][CTX_MAX_TOK][C] */
	float *hfin;                /* [CTX_MAX_TOK][C] post-LN state */
	float nll[CTX_MAX_TOK];     /* nll of target (next token) at each position */
	float mx[CTX_MAX_TOK];      /* softmax max / log-sum at each position: they depend on */
	float ls[CTX_MAX_TOK];      /* the hidden state only, so any target reuses them */
	uint8_t lv[CTX_MAX_TOK];    /* mx/ls valid for this position's current hidden state */
	float *logits;              /* [V] scratch */
} CtxCache;

static int ctx_cache_init(WhizModel *m, CtxCache *cc)
{
	memset(cc, 0, sizeof(*cc));
	size_t C = m->ctx_dim, per = (size_t)CTX_MAX_TOK * C;
	cc->K = (float *)malloc(per * m->ctx_layer * sizeof(float));
	cc->V = (float *)malloc(per * m->ctx_layer * sizeof(float));
	cc->hfin = (float *)malloc(per * sizeof(float));
	cc->logits = (float *)malloc((size_t)m->n_tok * sizeof(float));
	if (!cc->K || !cc->V || !cc->hfin || !cc->logits) return 1;
	return 0;
}

static void ctx_cache_free(CtxCache *cc)
{
	free(cc->K); free(cc->V); free(cc->hfin); free(cc->logits);
}

/* Compute positions p..t-1 given valid cached states for positions < p. */
static int ctx_extend(WhizModel *m, CtxCache *cc, const int *tok, uint32_t t, uint32_t p)
{
	uint32_t C = m->ctx_dim, nh = m->ctx_head, hd = C / nh, nn = t - p;
	if (nn == 0) return 0;
	size_t per = (size_t)CTX_MAX_TOK * C;
	/* h [nn,C] · xn [nn,C] · qkv [nn,3C] · acc [nn,C] · ff [nn,4C] · att [CTX_MAX_TOK] */
	float *buf = (float *)malloc(((size_t)nn * 10 * C + CTX_MAX_TOK) * sizeof(float));
	if (!buf) return 1;
	float *h = buf, *xn = h + (size_t)nn * C, *qkv = xn + (size_t)nn * C,
	      *acc = qkv + (size_t)nn * 3 * C, *ff = acc + (size_t)nn * C,
	      *att = ff + (size_t)nn * 4 * C;
	float inv_sqrt = 1.f / sqrtf((float)hd);

	for (uint32_t i = p; i < t; i++) {
		int it = tok[i];
		if (it < 0 || (uint32_t)it >= m->n_tok) it = TOK_UNK;
		uint32_t ipos = i < m->ctx_max_len ? i : m->ctx_max_len - 1;
		float *row = h + (size_t)(i - p) * C;
		for (uint32_t c = 0; c < C; c++)
			row[c] = m->ctx_tok[(size_t)it * C + c] + m->ctx_posit[(size_t)ipos * C + c];
	}
	/* Layer by layer over all new tokens at once: a token's attention only needs the
	 * layer's K/V of earlier tokens, which the batched projection has already written,
	 * and batching lets each weight row serve several tokens per pass. */
	for (uint32_t L = 0; L < m->ctx_layer; L++) {
		CtxLayer *cl = &m->layers[L];
		float *K = cc->K + per * L, *V = cc->V + per * L;
		for (uint32_t r = 0; r < nn; r++)
			layer_norm(h + (size_t)r * C, cl->ln1_g, cl->ln1_b, C, xn + (size_t)r * C);
		linear_n(cl->qkv_w, cl->qkv_b, xn, nn, 3 * C, C, qkv);
		for (uint32_t r = 0; r < nn; r++) {
			const float *q3 = qkv + (size_t)r * 3 * C;
			memcpy(K + (size_t)(p + r) * C, q3 + C, C * sizeof(float));
			memcpy(V + (size_t)(p + r) * C, q3 + 2 * C, C * sizeof(float));
		}
		for (uint32_t r = 0; r < nn; r++) {
			uint32_t i = p + r;
			const float *q3 = qkv + (size_t)r * 3 * C;
			float *a = acc + (size_t)r * C;
			for (uint32_t c = 0; c < C; c++) a[c] = 0.f;
			for (uint32_t hh = 0; hh < nh; hh++) {
				uint32_t off = hh * hd;
				const float *q = q3 + off;
				float mx = -1e30f;
				for (uint32_t j = 0; j <= i; j++) {
					float s = dot_f(q, K + (size_t)j * C + off, hd) * inv_sqrt;
					att[j] = s;
					if (s > mx) mx = s;
				}
				float sum = 0.f;
				for (uint32_t j = 0; j <= i; j++) { att[j] = expf(att[j] - mx); sum += att[j]; }
				float inv = 1.f / sum;
				float *o = a + off;
				for (uint32_t j = 0; j <= i; j++) {
					float wgt = att[j] * inv;
					const float *v = V + (size_t)j * C + off;
					for (uint32_t d = 0; d < hd; d++) o[d] += wgt * v[d];
				}
			}
		}
		linear_n(cl->o_w, cl->o_b, acc, nn, C, C, xn);       /* xn reused: projection out */
		for (uint32_t i = 0; i < nn * C; i++) h[i] += xn[i];
		for (uint32_t r = 0; r < nn; r++)
			layer_norm(h + (size_t)r * C, cl->ln2_g, cl->ln2_b, C, xn + (size_t)r * C);
		linear_n(cl->ff0_w, cl->ff0_b, xn, nn, 4 * C, C, ff);
		for (uint32_t i = 0; i < nn * 4 * C; i++) ff[i] = gelu_f(ff[i]);
		linear_n(cl->ff1_w, cl->ff1_b, ff, nn, C, 4 * C, xn);
		for (uint32_t i = 0; i < nn * C; i++) h[i] += xn[i];
	}
	for (uint32_t i = p; i < t; i++) {
		layer_norm(h + (size_t)(i - p) * C, m->out_ln_g, m->out_ln_b, C,
		           cc->hfin + (size_t)i * C);
		cc->lv[i] = 0;
	}
	free(buf);
	return 0;
}

/* Raw (unscaled) nll of one target at one position; 0 for an UNK target. */
static float ctx_pos_nll(WhizModel *m, CtxCache *cc, uint32_t i, int tgt)
{
	uint32_t C = m->ctx_dim, V = m->n_tok;
	if (tgt < 0 || (uint32_t)tgt >= V) tgt = TOK_UNK;
	if (tgt == TOK_UNK) return 0.f;
	const float *hi = cc->hfin + (size_t)i * C;
	if (!cc->lv[i]) {
		float mx = -1e30f;
		head_logits(hi, m->ctx_tok, V, C, cc->logits);
		for (uint32_t v = 0; v < V; v++)
			if (cc->logits[v] > mx) mx = cc->logits[v];
		double sum = 0.0;
		for (uint32_t v = 0; v < V; v++) sum += expf(cc->logits[v] - mx);
		cc->mx[i] = mx;
		cc->ls[i] = (float)log(sum);
		cc->lv[i] = 1;
	}
	return -((dot_f(hi, m->ctx_tok + (size_t)tgt * C, C) - cc->mx[i]) - cc->ls[i]);
}

/* Same value as ctx_path_cost(), reusing whatever prefix `cc` already holds. */
static float ctx_path_cost_cached(
	WhizModel *m, CtxCache *cc, const int *tok, const int *target, uint32_t t)
{
	if (!m->has_ctx || t == 0) return 0.f;
	if (t > CTX_MAX_TOK) t = CTX_MAX_TOK;
	uint32_t p = 0;
	while (p < t && (int)p < cc->n && tok[p] == cc->tok[p]) p++;
	if (p == t && (int)t == cc->n && t > 0) p = t;   /* identical sequence */
	if (p < t && ctx_extend(m, cc, tok, t, p)) return 0.f;
	memcpy(cc->tok, tok, t * sizeof(int));
	/* nll_i is reusable for i <= p-2 (its target tok[i+1] lies inside the prefix) */
	uint32_t reuse = p >= 1 ? p - 1 : 0;
	if (reuse > t) reuse = t;
	double nll = 0.0;
	for (uint32_t i = 0; i < t; i++) {
		if (i >= reuse) cc->nll[i] = ctx_pos_nll(m, cc, i, target[i]);
		nll += cc->nll[i];
	}
	cc->n = (int)t;
	return m->ctx_scale * (float)nll;
}

// ─── Dictionary ─────────────────────────────────────────────────────────────

typedef struct {
	char *read;
	char *word;
	char *base_word;  /* citation (基本形) surface */
	char *base_read;  /* citation (基本形) reading in hiragana */
	int32_t idx;
	int32_t lex;      /* unused in v5 */
	int16_t wclass;
	int16_t cost;
	int16_t typ;      /* forms.dic type id, or -1 for non-conjugating */
	int16_t form;     /* form id, 0 = stem (grow on the fly), -1 = plain */
} DicEntry;

/* forms.dic conjugation table (WUTF v3), used to grow stems on the fly. */
typedef struct {
	int16_t form_id;
	char *okuri_surface;  /* UTF-8; "" for bare stem forms */
	char *okuri_read;     /* UTF-8 katakana; "" when no okurigana */
} FormRow;

typedef struct {
	uint32_t type_id;
	uint32_t family;
	int n_forms;
	FormRow *rows;
} FormType;

typedef struct {
	FormType *types;
	int n_types;
	FormType *by_id[256];  /* type_id (0..72) → FormType*, NULL if absent */
} Forms;

typedef struct {
	DicEntry *entries;
	int n;
	int max_read_chars;
	int *order;       /* indices sorted by read */
	int *word_order;  /* indices sorted by word (surface) */
	Forms forms;      /* conjugation table */
	ConnectDic cd;    /* connect.dic table (WUTF v4 CNNX section) */
	int has_connect;
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

static void dic_add(
	WhizDic *d, int *cap,
	const char *read, const char *word,
	int wclass, int cost, int typ, int form, int lex)
{
	if (d->n >= *cap) {
		*cap *= 2;
		d->entries = (DicEntry *)realloc(d->entries, (size_t)(*cap) * sizeof(DicEntry));
	}
	DicEntry *e = &d->entries[d->n];
	e->idx = d->n++;
	e->read = strdup(read);
	e->word = strdup(word);
	e->base_word = NULL;  /* filled in dic_fill_base_words */
	e->base_read = NULL;
	e->wclass = (int16_t)wclass;
	e->cost = (int16_t)cost;
	e->typ = (int16_t)typ;
	e->form = (int16_t)form;
	e->lex = lex;
	int rc = utf8_nchars(e->read);
	if (rc > d->max_read_chars) d->max_read_chars = rc;
}

static FormType *forms_find(const Forms *F, int typ)
{
	if (typ < 0 || typ >= 256) return NULL;
	return F->by_id[typ];
}

/* okurigana surface of the citation form (form id 1), "" if unknown */
static const char *forms_base_okuri(const Forms *F, int typ)
{
	FormType *ft = forms_find(F, typ);
	if (!ft) return "";
	for (int i = 0; i < ft->n_forms; i++)
		if (ft->rows[i].form_id == 1) return ft->rows[i].okuri_surface;
	return "";
}

/* okurigana reading (katakana) of the citation form */
static const char *forms_base_okuri_read(const Forms *F, int typ)
{
	FormType *ft = forms_find(F, typ);
	if (!ft) return "";
	for (int i = 0; i < ft->n_forms; i++)
		if (ft->rows[i].form_id == 1) return ft->rows[i].okuri_read;
	return "";
}

/* Fill base_word and base_read for every entry.  Stems (typ>=0, form==0) get
 * word+citation_okurigana; base_read is kata→hira converted, matching
 * DicEntry.__post_init__ / WhizDic._add in whiz.py. */
static void dic_fill_base_words(WhizDic *d)
{
	for (int i = 0; i < d->n; i++) {
		DicEntry *e = &d->entries[i];
		if (e->typ >= 0 && e->form == 0) {
			const char *ok_s = forms_base_okuri(&d->forms, e->typ);
			const char *ok_r = forms_base_okuri_read(&d->forms, e->typ);
			size_t bl = strlen(e->word) + strlen(ok_s) + 1;
			char *bw = (char *)malloc(bl);
			snprintf(bw, bl, "%s%s", e->word, ok_s);
			e->base_word = bw;
			char kata[MAX_READ_KEY];
			snprintf(kata, sizeof(kata), "%s%s", e->read, ok_r);
			char hira[MAX_READ_KEY];
			kata_to_hira(kata, hira, sizeof(hira));
			e->base_read = strdup(hira);
		} else {
			e->base_word = strdup(e->word);
			char hira[MAX_READ_KEY];
			kata_to_hira(e->read, hira, sizeof(hira));
			e->base_read = strdup(hira);
		}
	}
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

/* Read the WUTF forms section (conjugation table) into d->forms. */
static int wutf_read_forms(const uint8_t **pp, const uint8_t *end, WhizDic *d)
{
	const uint8_t *p = *pp;
	uint32_t nt = 0;
	if (read_u32_mem(&p, end, &nt)) return 1;
	Forms *F = &d->forms;
	F->n_types = (int)nt;
	F->types = (FormType *)calloc(nt ? nt : 1, sizeof(FormType));
	for (uint32_t t = 0; t < nt; t++) {
		uint32_t tid = 0, fam = 0, nf = 0;
		if (read_u32_mem(&p, end, &tid) || read_u32_mem(&p, end, &fam)
			|| read_u32_mem(&p, end, &nf))
			return 1;
		FormType *ft = &F->types[t];
		ft->type_id = tid;
		ft->family = fam;
		ft->n_forms = (int)nf;
		ft->rows = (FormRow *)calloc(nf ? nf : 1, sizeof(FormRow));
		for (uint32_t r = 0; r < nf; r++) {
			if (p + 6 > end) return 1;
			int16_t fid = 0;
			uint16_t slen = 0, klen = 0;
			memcpy(&fid, p, 2); p += 2;
			memcpy(&slen, p, 2); p += 2;
			memcpy(&klen, p, 2); p += 2;
			if (p + slen + klen > end) return 1;
			FormRow *row = &ft->rows[r];
			row->form_id = fid;
			row->okuri_surface = (char *)malloc((size_t)slen + 1);
			memcpy(row->okuri_surface, p, slen);
			row->okuri_surface[slen] = 0; p += slen;
			row->okuri_read = (char *)malloc((size_t)klen + 1);
			memcpy(row->okuri_read, p, klen);
			row->okuri_read[klen] = 0; p += klen;
		}
		if (tid < 256) F->by_id[tid] = ft;
	}
	*pp = p;
	return 0;
}

static int wutf_vu(const uint8_t **pp, const uint8_t *end, uint32_t *out)
{
	uint32_t v = 0;
	for (int shift = 0; shift < 35; shift += 7) {
		if (*pp >= end) return 1;
		uint8_t b = *(*pp)++;
		v |= (uint32_t)(b & 0x7F) << shift;
		if (!(b & 0x80)) { *out = v; return 0; }
	}
	return 1;
}

#define SYMBOL_FORM (-2)
#define SYMBOL_LM_PENALTY 10000.f
/* Average per-token nll charged for a letter the LM cannot score (full-width Latin, Greek,
 * Cyrillic outside its vocabulary).  Mirrors whiz.py UNK_FOREIGN_NLL. */
#define UNK_FOREIGN_NLL 4.0f
#define SYMBOL_LOCAL_PENALTY 6000.f
/* A kana reading that resolves to a bare Latin word (ばー→bar) is an alias for the
 * candidate list; the LM scores plain English cheaply, so keep it from winning. */
#define LATIN_LOCAL_PENALTY 20000.f

/* UTF-8 dictionary (magic WUTF v3).  Carries the forms table so the runtime can
 * conjugate stems on the fly (matching whiz.py's expand_at), plus one lexeme id
 * per entry so stage 2 and stage 3 can look up real word embeddings. */
static int whiz_load_wutf_mem(const uint8_t *buf, size_t sz, WhizDic *d)
{
	memset(d, 0, sizeof(*d));
	if (sz < 16 || memcmp(buf, "WUTF", 4) != 0) return 1;
	const uint8_t *p = buf + 4, *end = buf + sz;
	uint32_t ver = 0, n = 0, maxr = 0;
	if (read_u32_mem(&p, end, &ver) || (ver < 3 || ver > 5)) {
		fprintf(stderr,
			"unsupported WUTF version %u (need 3, 4 or 5);\n"
			"        re-export with `python3 engine/whiz.py export-web`\n", ver);
		return 1;
	}
	if (read_u32_mem(&p, end, &n) || read_u32_mem(&p, end, &maxr)) return 1;
	int cap = (int)n + 64;
	if (cap < 1024) cap = 1024;
	d->entries = (DicEntry *)malloc((size_t)cap * sizeof(DicEntry));
	d->n = 0;
	d->max_read_chars = (int)maxr;
	if (ver == 5) {
		/* v5: front-coded, varint-packed entries (see whiz.py export_wutf_v4). */
		uint32_t nc = 0;
		if (read_u32_mem(&p, end, &nc) || nc > 65536 || p + (size_t)nc * 6 > end) return 1;
		const uint8_t *combo = p;
		p += (size_t)nc * 6;
		char read_u[512] = "", word_u[512];
		for (uint32_t i = 0; i < n; i++) {
			uint32_t shared, slen, wlen, ci, zc;
			if (wutf_vu(&p, end, &shared) || wutf_vu(&p, end, &slen)) return 1;
			if (shared > strlen(read_u) || shared + slen >= sizeof(read_u) ||
			    p + slen > end) return 1;
			memcpy(read_u + shared, p, slen); read_u[shared + slen] = 0; p += slen;
			if (wutf_vu(&p, end, &wlen) || wlen >= sizeof(word_u) || p + wlen > end) return 1;
			memcpy(word_u, p, wlen); word_u[wlen] = 0; p += wlen;
			if (wutf_vu(&p, end, &ci) || ci >= nc || wutf_vu(&p, end, &zc)) return 1;
			int16_t wclass, typ, form;
			memcpy(&wclass, combo + ci * 6, 2);
			memcpy(&typ, combo + ci * 6 + 2, 2);
			memcpy(&form, combo + ci * 6 + 4, 2);
			int16_t cost = (int16_t)((zc >> 1) ^ -(int32_t)(zc & 1));
			dic_add(d, &cap, read_u, word_u, wclass, cost, typ, form, 0);
		}
	} else
	{
	for (uint32_t i = 0; i < n; i++) {
		if (p + 4 > end) return 1;
		uint16_t rlen = 0, wlen = 0;
		memcpy(&rlen, p, 2); p += 2;
		memcpy(&wlen, p, 2); p += 2;
		if (p + rlen + wlen + 12 > end) return 1;
		char read_u[512], word_u[512];
		if (rlen >= sizeof(read_u) || wlen >= sizeof(word_u)) return 1;
		memcpy(read_u, p, rlen); read_u[rlen] = 0; p += rlen;
		memcpy(word_u, p, wlen); word_u[wlen] = 0; p += wlen;
		int16_t wclass, cost, typ, form;
		int32_t lex;
		memcpy(&wclass, p, 2); p += 2;
		memcpy(&cost, p, 2); p += 2;
		memcpy(&typ, p, 2); p += 2;
		memcpy(&form, p, 2); p += 2;
		memcpy(&lex, p, 4); p += 4;
		dic_add(d, &cap, read_u, word_u, wclass, cost, typ, form, lex);
	}
	}
	if (wutf_read_forms(&p, end, d)) {
		fprintf(stderr, "WUTF forms section truncated\n");
		return 1;
	}
	fprintf(stderr, "forms: %d conjugation types\n", d->forms.n_types);
	dic_fill_base_words(d);

	/* WUTF v4: read CNNX connect section */
	if (ver >= 4 && p + 4 <= end && memcmp(p, "CNNX", 4) == 0) {
		p += 4;
		uint32_t ns = 0;
		if (read_u32_mem(&p, end, &ns)) { whiz_finalize_index(d); return 1; }
		d->cd.n_strings = (int)ns;
		d->cd.strings = (char **)calloc(ns, sizeof(char *));
		for (uint32_t i = 0; i < ns; i++) {
			uint16_t slen = 0;
			if (p + 2 > end) { whiz_finalize_index(d); return 1; }
			memcpy(&slen, p, 2); p += 2;
			if (p + slen > end) { whiz_finalize_index(d); return 1; }
			d->cd.strings[i] = (char *)malloc((size_t)slen + 1);
			memcpy(d->cd.strings[i], p, slen);
			d->cd.strings[i][slen] = 0;
			p += slen;
		}
		uint32_t nr = 0;
		if (read_u32_mem(&p, end, &nr)) { whiz_finalize_index(d); return 1; }
		d->cd.n_rules = (int)nr;
		d->cd.rules = (CnnxRule *)malloc(nr * sizeof(CnnxRule));
		for (uint32_t i = 0; i < nr; i++) {
			CnnxRule *r = &d->cd.rules[i];
			if (p + 16 > end) { whiz_finalize_index(d); return 1; }
			memcpy(&r->lw, p, 2); p += 2;
			memcpy(&r->rw, p, 2); p += 2;
			r->lt = *(int8_t *)p; p++;
			r->lf = *(int8_t *)p; p++;
			r->rt = *(int8_t *)p; p++;
			r->rf = *(int8_t *)p; p++;
			r->spec = *p; p++;
			r->_pad = *p; p++;
			memcpy(&r->lword_idx, p, 2); p += 2;
			memcpy(&r->rword_idx, p, 2); p += 2;
			memcpy(&r->cost, p, 2); p += 2;
			memcpy(&r->_pad2, p, 2); p += 2;
		}
		uint32_t np = 0;
		if (read_u32_mem(&p, end, &np)) { whiz_finalize_index(d); return 1; }
		d->cd.n_pairs = (int)np;
		d->cd.pairs = (CnnxPair *)malloc(np * sizeof(CnnxPair));
		for (uint32_t i = 0; i < np; i++) {
			CnnxPair *q = &d->cd.pairs[i];
			if (p + 12 > end) { whiz_finalize_index(d); return 1; }
			memcpy(&q->lw, p, 2); p += 2;
			memcpy(&q->rw, p, 2); p += 2;
			memcpy(&q->start, p, 4); p += 4;
			memcpy(&q->count, p, 4); p += 4;
		}
		d->has_connect = 1;
		fprintf(stderr, "connect: %d rules, %d pairs\n", d->cd.n_rules, d->cd.n_pairs);
	}

	whiz_finalize_index(d);
	return 0;
}

static int whiz_load_mem(const uint8_t *buf, size_t sz, WhizDic *d)
{
	if (sz >= 4 && memcmp(buf, "WUTF", 4) == 0)
		return whiz_load_wutf_mem(buf, sz, d);
	/* The raw EUC-JP whiz.dic cannot carry the forms table that on-the-fly
	 * conjugation needs.  Export the UTF-8 dictionary and use that instead. */
	fprintf(stderr,
		"error: expected a WUTF dictionary (out/whiz.web.dic).\n"
		"        the raw EUC-JP whiz.dic is unsupported by the C runtime;\n"
		"        run `python3 engine/whiz.py export-web` and pass out/whiz.web.dic.\n");
	return 1;
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
		free(d->entries[i].base_word);
		free(d->entries[i].base_read);
	}
	free(d->entries);
	free(d->order);
	free(d->word_order);
	for (int t = 0; t < d->forms.n_types; t++) {
		FormType *ft = &d->forms.types[t];
		for (int r = 0; r < ft->n_forms; r++) {
			free(ft->rows[r].okuri_surface);
			free(ft->rows[r].okuri_read);
		}
		free(ft->rows);
	}
	free(d->forms.types);
	/* free ConnectDic */
	for (int i = 0; i < d->cd.n_strings; i++) free(d->cd.strings[i]);
	free(d->cd.strings);
	free(d->cd.rules);
	free(d->cd.pairs);
}

/* ─── user learning ──────────────────────────────────────────────────────────
 * Remembers (reading, word) pairs the user committed.  Each pair lowers the
 * word's cost once, in the lattice (so it is found and listed first).  It is not
 * re-applied after the LM rescoring: a few commits must not override the LM.
 * Persisted as an append-only text file: "reading<TAB>word" per line. */
#define LEARN_SLOTS 8192
typedef struct { char *read, *word; uint16_t count; } LearnEnt;
static LearnEnt g_learn[LEARN_SLOTS];
static int g_learn_n;
static char g_learn_path[512];

static uint32_t learn_hash(const char *r, const char *w)
{
	uint32_t h = 2166136261u;
	for (; *r; r++) h = (h ^ (uint8_t)*r) * 16777619u;
	h = (h ^ 0x1f) * 16777619u;
	for (; *w; w++) h = (h ^ (uint8_t)*w) * 16777619u;
	return h;
}

static LearnEnt *learn_find(const char *r, const char *w, int create)
{
	if (!g_learn_n && !create) return NULL;
	uint32_t i = learn_hash(r, w) & (LEARN_SLOTS - 1);
	for (int probe = 0; probe < LEARN_SLOTS; probe++, i = (i + 1) & (LEARN_SLOTS - 1)) {
		LearnEnt *e = &g_learn[i];
		if (!e->read) {
			if (!create || g_learn_n >= LEARN_SLOTS / 2) return NULL;
			e->read = strdup(r); e->word = strdup(w); e->count = 0;
			g_learn_n++;
			return e;
		}
		if (!strcmp(e->read, r) && !strcmp(e->word, w)) return e;
	}
	return NULL;
}

/* `read` is the katakana span, as stored in Node.read. */
static float learn_bonus(const char *read, const char *word)
{
	LearnEnt *e = learn_find(read, word, 0);
	if (!e) return 0.f;
	float b = 2000.f;
	for (unsigned n = e->count; n > 1; n >>= 1) b += 400.f;
	return b > 4000.f ? 4000.f : b;
}

static void learn_note(const char *yomi, const char *word)
{
	char kata[MAX_READ_KEY];
	hira_to_kata(yomi, kata, sizeof(kata));
	LearnEnt *e = learn_find(kata, word, 1);
	if (e && e->count < 60000) e->count++;
}

typedef struct {
	char word[MAX_WORD];
	char read[MAX_READ_KEY];
	char base_word[MAX_WORD];     /* citation-form surface for connect lookup */
	char base_read[MAX_READ_KEY]; /* citation-form reading (hiragana) */
	int wclass, typ, form, cost, lex, entry_idx;
	float lexical_cost;
	int start, end;
	int read_len;
} Node;

/* Mirrors whiz.py `_ok_surface`: a kana reading never yields a bare ASCII or
 * full-width symbol (と→＆, いち→１, えー→Ａ) or a punctuation/shape mark.
 * Curated emoticon/symbol entries (SYMBOL_FORM) are candidates by design, so
 * only the ASCII and punctuation rules apply to them. */
static int bad_surface(const char *read, const char *word, int form)
{
	static const char *bad[] = {
		"、", "。", "．", "，", "・", "：", "；", "！", "？", NULL
	};
	static const char *bad_marks[] = {
		"…", "―", "☆", "★", "※", "○", "●", "□", "■",
		"▲", "△", "▽", "▼", "◇", "◆", NULL
	};
	for (int i = 0; bad[i]; i++)
		if (strcmp(word, bad[i]) == 0) return 1;
	if (!word[0]) return 1;
	if (form != SYMBOL_FORM)
		for (int i = 0; bad_marks[i]; i++)
			if (strcmp(word, bad_marks[i]) == 0) return 1;
	if (read[0]) {
		int rl = 0, wl = 0;
		int rcp = utf8_decode((const unsigned char *)read, &rl);
		int is_kana = (rcp >= 0x30A0 && rcp <= 0x30FF) || (rcp >= 0x3040 && rcp <= 0x309F);
		if (is_kana) {
			if ((unsigned char)word[0] < 0x80 && word[1] == 0) return 1;
			if (form != SYMBOL_FORM) {
				int wcp = utf8_decode((const unsigned char *)word, &wl);
				if (wl > 0 && word[wl] == 0 && wcp >= 0xFF00 && wcp <= 0xFF60)
					return 1;
				/* arrows / math / shapes / dingbats / emoji: candidates only
				 * (みぎ→→ must not win a sentence); mirrors _is_symbol_char */
				if (wl > 0 && word[wl] == 0 &&
				    ((wcp >= 0x2190 && wcp <= 0x22FF) || (wcp >= 0x25A0 && wcp <= 0x25FF) ||
				     (wcp >= 0x2600 && wcp <= 0x27BF) || (wcp >= 0x2B00 && wcp <= 0x2BFF) ||
				     (wcp >= 0x1F000 && wcp <= 0x1FFFF) ||
				     (wcp >= 0x0370 && wcp <= 0x04FF)))
					return 1;
			}
		}
	}
	return 0;
}

static void fill_node(
	Node *nd, const DicEntry *e, const char *span,
	int start, int end, WhizModel *m)
{
	(void)m;
	strncpy(nd->word, e->word, MAX_WORD - 1);
	nd->word[MAX_WORD - 1] = 0;
	strncpy(nd->read, span, MAX_READ_KEY - 1);
	nd->read[MAX_READ_KEY - 1] = 0;
	strncpy(nd->base_word, e->base_word ? e->base_word : e->word, MAX_WORD - 1);
	nd->base_word[MAX_WORD - 1] = 0;
	strncpy(nd->base_read, e->base_read ? e->base_read : e->read, MAX_READ_KEY - 1);
	nd->base_read[MAX_READ_KEY - 1] = 0;
	nd->wclass = e->wclass;
	nd->typ = e->typ;
	nd->form = e->form;
	nd->cost = e->cost;
	nd->entry_idx = e->idx;
	nd->lexical_cost = (float)e->cost - learn_bonus(nd->read, nd->word);
	if (nd->lexical_cost < 0.f) nd->lexical_cost = 0.f;
	nd->lex = e->lex;
	nd->start = start;
	nd->end = end;
	nd->read_len = end - start;
}

/* First dictionary index whose read == span (binary search over d->order). */
static int dic_lower_bound(WhizDic *d, const char *span)
{
	int lo = 0, hi = d->n;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		int c = strcmp(d->entries[d->order[mid]].read, span);
		if (c < 0) lo = mid + 1;
		else hi = mid;
	}
	return lo;
}

/* Grow a stem into inflected forms whose okurigana continue `yomi` at start+L.
 * Mirrors whiz.py's _conjugate / expand_at.  Returns number of nodes appended. */
static int conjugate_stem(
	WhizDic *d, WhizModel *m __attribute__((unused)), const DicEntry *e,
	const char *yomi, const char *original,
	int start, int L, int nchars, const char *stem_span,
	Node *out, int out_cap)
{
	FormType *ft = forms_find(&d->forms, e->typ);
	if (!ft) return 0;
	int n = 0;
	int base_char = start + L;
	for (int r = 0; r < ft->n_forms && n < out_cap; r++) {
		FormRow *row = &ft->rows[r];
		int ok_chars = row->okuri_read[0] ? utf8_nchars(row->okuri_read) : 0;
		int end = base_char + ok_chars;
		if (end > nchars) continue;
		if (ok_chars > 0) {
			char buf[MAX_READ_KEY];
			utf8_slice(yomi, base_char, ok_chars, buf, sizeof(buf));
			if (strcmp(buf, row->okuri_read) != 0) continue;
		}
		if (end < nchars && is_small_kana(peek_cp_at_char(yomi, end)))
			continue;
		if (span_has_passthrough(original, start, end - start))
			continue;
		Node *nd = &out[n];
		snprintf(nd->read, MAX_READ_KEY, "%s%s", stem_span, row->okuri_read);
		snprintf(nd->word, MAX_WORD, "%s%s", e->word, row->okuri_surface);
		if (bad_surface(nd->read, nd->word, -1)) continue;
		/* base_word/base_read follow the stem's citation form */
		strncpy(nd->base_word, e->base_word ? e->base_word : e->word, MAX_WORD-1);
		nd->base_word[MAX_WORD-1] = 0;
		strncpy(nd->base_read, e->base_read ? e->base_read : e->read, MAX_READ_KEY-1);
		nd->base_read[MAX_READ_KEY-1] = 0;
		nd->wclass = e->wclass;
		nd->typ = e->typ;
		nd->form = row->form_id;
		nd->cost = e->cost;
		nd->entry_idx = e->idx;
		nd->lexical_cost = (float)e->cost - learn_bonus(nd->read, nd->word);
		if (nd->lexical_cost < 0.f) nd->lexical_cost = 0.f;
		nd->lex = e->lex;
		nd->start = start;
		nd->end = end;
		nd->read_len = end - start;
		n++;
	}
	return n;
}

/* Candidates starting at char `start`, conjugating stems against the input.
 * Enumerates every span length internally so a stem's node can end past
 * start+L.  Replaces the fixed-span collect_cands. */
static int collect_cands_at(
	WhizDic *d, WhizModel *m, const char *yomi, const char *original,
	int start, int nchars, Node *out, int out_cap)
{
	int n = 0;
	int lim = d->max_read_chars;
	if (lim > nchars - start) lim = nchars - start;
	if (lim > 24) lim = 24;
	for (int L = lim; L >= 1 && n < out_cap; L--) {
		if (span_has_passthrough(original, start, L)) continue;
		char span[MAX_READ_KEY];
		utf8_slice(yomi, start, L, span, sizeof(span));
		for (int i = dic_lower_bound(d, span); i < d->n && n < out_cap; i++) {
			DicEntry *e = &d->entries[d->order[i]];
			if (strcmp(e->read, span) != 0) break;
			if (e->typ >= 0 && e->form == 0) {
				n += conjugate_stem(d, m, e, yomi, original,
					start, L, nchars, span, out + n, out_cap - n);
			} else {
				int end = start + L;
				if (end < nchars && is_small_kana(peek_cp_at_char(yomi, end)))
					continue;
				if (bad_surface(span, e->word, e->form)) continue;
				fill_node(&out[n++], e, span, start, end, m);
			}
		}
	}
	return n;
}

/* True if any word (plain or a matching stem conjugation) starts at `start`. */
static int has_dictionary_match(
	WhizDic *d, const char *yomi, const char *original,
	int start, int nchars)
{
	int lim = d->max_read_chars;
	if (lim > nchars - start) lim = nchars - start;
	if (lim > 24) lim = 24;
	for (int L = lim; L >= 1; L--) {
		if (span_has_passthrough(original, start, L)) continue;
		char span[MAX_READ_KEY];
		utf8_slice(yomi, start, L, span, sizeof(span));
		for (int i = dic_lower_bound(d, span); i < d->n; i++) {
			DicEntry *e = &d->entries[d->order[i]];
			if (strcmp(e->read, span) != 0) break;
			if (e->typ >= 0 && e->form == 0) {
				/* stem: needs a form whose okurigana continues the input */
				FormType *ft = forms_find(&d->forms, e->typ);
				if (!ft) continue;
				for (int r = 0; r < ft->n_forms; r++) {
					FormRow *row = &ft->rows[r];
					int ok = row->okuri_read[0] ? utf8_nchars(row->okuri_read) : 0;
					int end = start + L + ok;
					if (end > nchars) continue;
					if (ok > 0) {
						char buf[MAX_READ_KEY];
						utf8_slice(yomi, start + L, ok, buf, sizeof(buf));
						if (strcmp(buf, row->okuri_read) != 0) continue;
					}
					if (end < nchars && is_small_kana(peek_cp_at_char(yomi, end)))
						continue;
					return 1;
				}
			} else {
				if (start + L < nchars
					&& is_small_kana(peek_cp_at_char(yomi, start + L)))
					continue;
				if (!bad_surface(span, e->word, e->form)) return 1;
			}
		}
	}
	return 0;
}

static void fill_unknown_node(
	Node *nd, const char *yomi, const char *original,
	int start, int end)
{
	utf8_slice(original, start, end - start, nd->word, MAX_WORD);
	utf8_slice(yomi, start, end - start, nd->read, MAX_READ_KEY);
	nd->wclass = 3;
	nd->typ = -1;
	nd->form = -1;
	nd->cost = 3500;
	nd->entry_idx = -1;
	nd->lexical_cost = 3500.f;
	nd->lex = oov_lex(nd->word);
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
	WhizDic *d, WhizModel *m, const char *yomi, const char *original,
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
			/* 片仮名で始まった連なりの内側では長音符も片仮名の一部。 */
			while (end < nchars && end - i < 24
				&& (passthrough_kind_at(original, end) == kind
					|| (kind == 2
						&& is_katakana_cp(
							peek_cp_at_char(original, end)))))
				end++;
			fill_unknown_node(&edges[i][0], yomi, original, i, end);
			n_edges[i] = 1;
			continue;
		}

		/* longest-first enumeration; stems are conjugated on the fly. */
		int nt = collect_cands_at(d, m, yomi, original, i, nchars, tmp, 512);
		if (nt == 0) {
			int end = i + 1;
			while (end < nchars && end - i < 24) {
				if (passthrough_kind_at(original, end)) break;
				if (has_dictionary_match(d, yomi, original, end, nchars)) break;
				end++;
			}
			fill_unknown_node(&edges[i][0], yomi, original, i, end);
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

static int is_latin_word(const char *w)
{
	int letters = 0;
	for (const unsigned char *p = (const unsigned char *)w; *p; p++) {
		if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) letters++;
		else if (*p >= 0x80) return 0;
	}
	return letters > 0;
}

static float word_cost_f(const Node *e, WhizModel *m)
{
	float c = e->lexical_cost;
	int L = e->read_len;
	if (L == 1 && !is_particle(e->word) && c < m->uni_floor)
		c = m->uni_floor + c;
	if (L >= 3 && (float)e->cost >= m->rare_cost)
		c += m->rare_long;
	if (e->form == SYMBOL_FORM) c += SYMBOL_LOCAL_PENALTY;  /* candidates only, never auto-picked */
	if (is_latin_word(e->word)) c += LATIN_LOCAL_PENALTY;
	return c;
}

static float length_bonus_f(const Node *e, WhizModel *m)
{
	int L = e->read_len;
	if (L <= 1) return 0.f;
	return m->mora * (float)L + m->mora2 * (float)(L * L);
}

static float short_penalty_f(
	const Node *e, const Node *edges_at, int n_at, WhizModel *m)
{
	/* Stem-like conjugated forms (未然/連用/タ接続/形容詞連用/仮定) are not
	 * short fragments of a longer noun that shares a reading prefix
	 * (咲い vs 最低).  A longer noun that swallows a case-particle mora
	 * (花傘 ⊃ 花+が) must not punish the short noun either. */
	if (is_particle(e->word)) return 0.f;
	if (e->typ >= 0) {
		int f = e->form;
		if (f == 6 || f == 12 || f == 13 || f == 14 || f == 18)
			return 0.f;
	}
	int L = e->read_len;
	float pen = 0.f;
	for (int i = 0; i < n_at; i++) {
		int ol = edges_at[i].read_len;
		if (ol <= L) continue;
		if ((float)edges_at[i].cost >= m->rare_cost) continue;
		/* Skip when longer reading continues with ハ/ガ/ヲ/ニ/ヘ/モ. */
		{
			const char *p = edges_at[i].read;
			int k = 0;
			while (*p && k < L) {
				unsigned char c = (unsigned char)*p;
				if (c < 0x80) p += 1;
				else if ((c & 0xE0) == 0xC0) p += 2;
				else if ((c & 0xF0) == 0xE0) p += 3;
				else if ((c & 0xF8) == 0xF0) p += 4;
				else p += 1;
				k++;
			}
			if (*p) {
				/* Katakana case particles: ハガヲニヘモ */
				static const char *case_p[] = {
					"ハ", "ガ", "ヲ", "ニ", "ヘ", "モ", NULL
				};
				int swallowed = 0;
				for (int j = 0; case_p[j]; j++) {
					size_t n = strlen(case_p[j]);
					if (strncmp(p, case_p[j], n) == 0) {
						swallowed = 1;
						break;
					}
				}
				if (swallowed) continue;
			}
		}
		if ((float)edges_at[i].cost <= (float)e->cost + m->short_margin)
			pen += m->short_pen * (float)(ol - L);
	}
	if (pen > 4000.f) pen = 4000.f;
	return pen;
}

static float step_cost(
	WhizDic *d, WhizModel *m,
	const Node *left, /* NULL = BOS */
	const Node *e,
	const Node *edges_at, int n_at)
{
	int lw, lt, lf;
	const char *lword, *lread;
	if (!left) {
		lw = BOS_WCLASS; lt = -1; lf = -1;
		lword = "*"; lread = "*";
	} else {
		lw = left->wclass; lt = left->typ; lf = left->form;
		lword = left->base_word; lread = left->base_read;
	}
	float cc = connect_cost_table(
		d->has_connect ? &d->cd : NULL,
		lw, lt, lf, lword, lread,
		e->wclass, e->typ, e->form, e->base_word, e->base_read);
	float cost = word_cost_f(e, m) + cc - length_bonus_f(e, m) + m->frag
		+ short_penalty_f(e, edges_at, n_at, m);
	/* Single-mora stemlike leftover (花傘+い vs 花+が+咲い). */
	if (e->typ >= 0 && e->read_len == 1 && !is_particle(e->word)) {
		int f = e->form;
		if (f == 6 || f == 12 || f == 13 || f == 14 || f == 18)
			cost += 250.f;
	}
	return cost;
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
/* Stage 3 costs a softmax over the whole lexicon per word.  It is worth it for
 * homophones, but callers on a tight latency budget can turn it off. */
static int g_ctx_enabled = 1;
static int g_lm_cache = 1;                    /* shared-prefix LM cache (0 = reference path) */
static int g_rescore_max = CTX_RESCORE_MAX;   /* n-best paths rescored by the LM (<= MAX_BEAM) */

/* Search mode.  1 = anchored LM beam (mirrors whiz.py lm_beam), 0 = rescore the pass-1
 * N-best list with the LM (the original C behaviour).  Select with `-s` / whiz_set_search. */
static int g_search = 1;
static int g_lm_beam = 8;     /* hypotheses carried between positions (whiz.py LM_BEAM) */
static int g_lm_cands = 8;    /* edges per hypothesis the LM is asked about (LM_CANDS) */
#define ANCHOR_NBEST 8        /* pass-1 paths that must agree on a boundary to anchor it */
#define LM_EXT_MAX   (MAX_BEAM * 16)

typedef struct {
	float cost;
	int parent;               /* arena index; -1 for the root */
	int pos, eidx;            /* the edge this hypothesis ended with: edges[pos][eidx] */
	int ntok;
	int tok[CTX_MAX_TOK];     /* morph tokens so far, BOS excluded */
} LmHyp;

typedef struct {
	int hyp, eidx, ntail, npre, t;
	float local, fc, nll;
	int in[CTX_MAX_TOK];      /* BOS pre tail */
} LmExt;

/* Raw nll of the targets in[npre+1 .. npre+ntail] (or of EOS when `eos`), reusing the
 * prefix states already held by `cc`.  Same quantity as whiz.py context_nll_tail. */
static float ctx_tail_nll(WhizModel *m, CtxCache *cc, const int *in, uint32_t t,
                          uint32_t npre, int ntail, int eos)
{
	uint32_t p = 0;
	while (p < t && (int)p < cc->n && in[p] == cc->tok[p]) p++;
	if (p < t && ctx_extend(m, cc, in, t, p)) return 0.f;
	memcpy(cc->tok, in, t * sizeof(int));
	cc->n = (int)t;
	double nll = 0.0;
	if (eos) nll = ctx_pos_nll(m, cc, npre, TOK_EOS);
	else for (int k = 0; k < ntail; k++) nll += ctx_pos_nll(m, cc, npre + (uint32_t)k, in[npre + 1 + (uint32_t)k]);
	return (float)nll;
}

static const LmHyp *g_lm_arena;
static int lm_hyp_cmp(const void *a, const void *b)
{
	float x = g_lm_arena[*(const int *)a].cost, y = g_lm_arena[*(const int *)b].cost;
	return (x > y) - (x < y);
}
static int lm_ext_ptr_cmp(const void *a, const void *b)
{
	const LmExt *x = *(LmExt *const *)a, *y = *(LmExt *const *)b;
	return tok_seq_cmp(x->in, x->t, y->in, y->t);
}
static int lm_ext_local_cmp(const void *a, const void *b)
{
	float x = ((const LmExt *)a)->local, y = ((const LmExt *)b)->local;
	return (x > y) - (x < y);
}

/* Anchored LM beam.  Pass 1 fixes the boundaries every one of its top paths agrees on;
 * inside them the LM is part of the search, so homophones compete directly instead of
 * being re-ranked from a short local N-best.  Returns a malloc'd best path, or NULL to let
 * the caller fall back to N-best rescoring. */
static Beam *lm_beam_search(WhizDic *dic, WhizModel *m, int nchars,
	Node (*edges)[MAX_EDGES_POS], const int *n_edges,
	const Beam *first, int n_first, int beam)
{
	if (beam > MAX_BEAM) beam = MAX_BEAM;
	int cands = g_lm_cands < 1 ? 1 : (g_lm_cands > 16 ? 16 : g_lm_cands);

	/* anchors: boundaries present in every one of the top paths */
	char *anchor = calloc((size_t)nchars + 1, 1);
	int *cnt = calloc((size_t)nchars + 1, sizeof(int));
	int np = 0;
	for (int i = 0; i < n_first && i < ANCHOR_NBEST; i++) {
		if (first[i].n == 0 || first[i].cost >= 1e29f) continue;
		np++;
		for (int k = 0; k < first[i].n; k++) {
			int e = first[i].nodes[k].end;
			if (e >= 0 && e <= nchars) cnt[e]++;
		}
	}
	if (!anchor || !cnt || np == 0) { free(anchor); free(cnt); return NULL; }
	for (int k = 1; k < nchars; k++) anchor[k] = cnt[k] == np;
	free(cnt);

	size_t cap = (size_t)nchars * (size_t)(beam * cands) + 8;
	LmHyp *ar = malloc(cap * sizeof(LmHyp));
	int bcap = beam * 8 + beam * cands + 8;
	int *bl = malloc((size_t)(nchars + 1) * (size_t)bcap * sizeof(int));
	int *bn = calloc((size_t)nchars + 1, sizeof(int));
	LmExt *ext = malloc((size_t)LM_EXT_MAX * sizeof(LmExt));
	LmExt **eord = malloc((size_t)LM_EXT_MAX * sizeof(LmExt *));
	CtxCache cc;
	int cc_ok = ctx_cache_init(m, &cc) == 0;
	Beam *res = NULL;
	if (!ar || !bl || !bn || !ext || !eord || !cc_ok) goto done;
	g_lm_arena = ar;

	size_t na = 0;
	ar[na] = (LmHyp){ .cost = 0.f, .parent = -1, .pos = -1, .eidx = -1, .ntok = 0 };
	na++;
	bl[0] = 0; bn[0] = 1;

	for (int pos = 0; pos < nchars; pos++) {
		int *L = bl + (size_t)pos * bcap;
		if (bn[pos] == 0 || n_edges[pos] == 0) continue;
		qsort(L, (size_t)bn[pos], sizeof(int), lm_hyp_cmp);
		if (bn[pos] > beam) bn[pos] = beam;

		int cidx[MAX_EDGES_POS], nc = 0;
		for (int e = 0; e < n_edges[pos]; e++) {
			int end = edges[pos][e].end;
			if (end > nchars) continue;
			int straddle = 0;
			for (int a = pos + 1; a < end; a++) if (anchor[a]) { straddle = 1; break; }
			if (!straddle) cidx[nc++] = e;
		}
		if (nc == 0)   /* anchors disagree with what the lattice can spell here */
			for (int e = 0; e < n_edges[pos]; e++)
				if (edges[pos][e].end <= nchars) cidx[nc++] = e;
		if (nc == 0) continue;

		int nx = 0;
		for (int bi = 0; bi < bn[pos]; bi++) {
			int h = L[bi];
			const Node *left = h > 0 ? &edges[ar[h].pos][ar[h].eidx] : NULL;
			LmExt tmp[MAX_EDGES_POS];
			for (int ci = 0; ci < nc; ci++) {
				LmExt *x = &tmp[ci];
				x->hyp = h; x->eidx = cidx[ci];
				x->local = ar[h].cost + step_cost(dic, m, left, &edges[pos][cidx[ci]],
				                                  edges[pos], n_edges[pos]);
			}
			qsort(tmp, (size_t)nc, sizeof(LmExt), lm_ext_local_cmp);
			for (int ci = 0; ci < nc && ci < cands && nx < LM_EXT_MAX; ci++) {
				LmExt *x = &ext[nx];
				x->hyp = tmp[ci].hyp; x->eidx = tmp[ci].eidx; x->local = tmp[ci].local;
				const Node *nd = &edges[pos][x->eidx];
				int tail[CTX_MAX_TOK];
				int nt = morph_tokenize(m, nd->word, tail, CTX_MAX_TOK - 1);
				if (nt < 0) nt = 0;
				if (nt > CTX_MAX_TOK - 1) nt = CTX_MAX_TOK - 1;
				int room = CTX_MAX_TOK - 1 - nt;
				int npre = ar[x->hyp].ntok < room ? ar[x->hyp].ntok : room;
				x->ntail = nt; x->npre = npre; x->t = 1 + npre + nt;
				x->in[0] = TOK_BOS;
				memcpy(x->in + 1, ar[x->hyp].tok + ar[x->hyp].ntok - npre, (size_t)npre * sizeof(int));
				memcpy(x->in + 1 + npre, tail, (size_t)nt * sizeof(int));
				x->fc = UNK_FOREIGN_NLL * (float)foreign_char_count(m, nd->word);
				nx++;
			}
		}
		/* score in token order so neighbours share their longest prefixes */
		for (int i = 0; i < nx; i++) eord[i] = &ext[i];
		qsort(eord, (size_t)nx, sizeof(LmExt *), lm_ext_ptr_cmp);
		for (int i = 0; i < nx; i++) {
			LmExt *x = eord[i];
			x->nll = x->ntail > 0 ? ctx_tail_nll(m, &cc, x->in, (uint32_t)x->t, (uint32_t)x->npre, x->ntail, 0) : 0.f;
		}
		for (int i = 0; i < nx && na < cap; i++) {
			LmExt *x = &ext[i];
			const Node *nd = &edges[pos][x->eidx];
			int end = nd->end;
			if (bn[end] >= bcap) continue;
			LmHyp *hn = &ar[na];
			const LmHyp *hp = &ar[x->hyp];
			hn->cost = x->local + m->ctx_scale * (x->nll + x->fc);
			hn->parent = x->hyp; hn->pos = pos; hn->eidx = x->eidx;
			int keep = hp->ntok + x->ntail;
			int drop = keep > CTX_MAX_TOK - 1 ? keep - (CTX_MAX_TOK - 1) : 0;
			int from = drop < hp->ntok ? drop : hp->ntok;
			hn->ntok = 0;
			for (int k = from; k < hp->ntok; k++) hn->tok[hn->ntok++] = hp->tok[k];
			for (int k = 0; k < x->ntail; k++) {
				if (hn->ntok >= CTX_MAX_TOK) break;
				hn->tok[hn->ntok++] = x->in[1 + x->npre + k];
			}
			bl[(size_t)end * bcap + bn[end]++] = (int)na++;
		}
		for (int k = pos + 1; k <= nchars; k++)
			if (bn[k] > beam * 8) {
				qsort(bl + (size_t)k * bcap, (size_t)bn[k], sizeof(int), lm_hyp_cmp);
				bn[k] = beam * 4;
			}
	}

	if (bn[nchars] == 0) goto done;
	{
		int *F = bl + (size_t)nchars * bcap;
		qsort(F, (size_t)bn[nchars], sizeof(int), lm_hyp_cmp);
		int nf = bn[nchars] < (beam > 16 ? beam : 16) * 2 ? bn[nchars] : (beam > 16 ? beam : 16) * 2;
		int best = -1;
		float best_c = 1e30f;
		for (int i = 0; i < nf; i++) {
			const LmHyp *h = &ar[F[i]];
			if (h->parent < 0) continue;
			const Node *last = &edges[h->pos][h->eidx];
			float c = h->cost + connect_cost_table(
				dic->has_connect ? &dic->cd : NULL,
				last->wclass, last->typ, last->form, last->base_word, last->base_read,
				EOS_WCLASS, -1, -1, "*", "*");
			int npre = h->ntok < CTX_MAX_TOK - 2 ? h->ntok : CTX_MAX_TOK - 2;
			int in[CTX_MAX_TOK];
			in[0] = TOK_BOS;
			memcpy(in + 1, h->tok + h->ntok - npre, (size_t)npre * sizeof(int));
			c += m->ctx_scale * ctx_tail_nll(m, &cc, in, (uint32_t)(1 + npre), (uint32_t)npre, 0, 1);
			if (c < best_c) { best_c = c; best = F[i]; }
		}
		if (best < 0) goto done;
		int len = 0;
		for (int h = best; h > 0; h = ar[h].parent) len++;
		if (len >= MAX_PATH) goto done;
		res = malloc(sizeof(Beam));
		if (!res) goto done;
		res->cost = best_c;
		res->n = len;
		int k = len;
		for (int h = best; h > 0; h = ar[h].parent)
			res->nodes[--k] = edges[ar[h].pos][ar[h].eidx];
		if (g_verbose) fprintf(stderr, "  lm-beam: %d anchors-voted paths, best=%.1f\n", np, best_c);
	}
done:
	if (cc_ok) ctx_cache_free(&cc);
	free(ar); free(bl); free(bn); free(ext); free(eord); free(anchor);
	return res;
}

static int convert(
	WhizDic *dic, WhizModel *m,
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
				float sc = b->cost + step_cost(dic, m, left, cand, edges[pos], n_edges[pos]);
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
	/* Close each path with EOS, then order them: this is the N-best that
	 * stage 3 rescores. */
	for (int i = 0; i < nb[nchars]; i++) {
		Beam *b = &beams[nchars][i];
		if (b->n == 0) { b->cost = 1e30f; continue; }
		b->cost += connect_cost_table(
			dic->has_connect ? &dic->cd : NULL,
			b->nodes[b->n-1].wclass, b->nodes[b->n-1].typ,
			b->nodes[b->n-1].form, b->nodes[b->n-1].base_word, b->nodes[b->n-1].base_read,
			EOS_WCLASS, -1, -1, "*", "*");
	}
	qsort(beams[nchars], (size_t)nb[nchars], sizeof(Beam), beam_cmp_asc);

	/* Pass ②: add ctx_scale * sum -log P(t_j | t_<j) over the path surface. */
	float best_c = beams[nchars][0].cost;
	int best_i = 0;
	Beam *lm_best = NULL;
	if (g_search == 1 && m->has_ctx && g_ctx_enabled)
		lm_best = lm_beam_search(dic, m, nchars, edges, n_edges, beams[nchars], nb[nchars], g_lm_beam);
	if (lm_best) best_c = lm_best->cost;
	int n_rescore = nb[nchars] < g_rescore_max ? nb[nchars] : g_rescore_max;
	if (!lm_best && m->has_ctx && g_ctx_enabled && n_rescore > 1) {
		best_c = 1e30f;
		/* Phase 1: token sequences of all rescored paths. */
		static int path_tok[MAX_BEAM][CTX_MAX_TOK], path_tgt[MAX_BEAM][CTX_MAX_TOK];
		int path_t[MAX_BEAM], order[MAX_BEAM];
		float lm_cost[MAX_BEAM];
		int n_paths = 0;
		for (int i = 0; i < n_rescore; i++) {
			Beam *b = &beams[nchars][i];
			path_t[i] = 0;
			lm_cost[i] = 0.f;
			if (b->n == 0) continue;
			char surface[MAX_YOMI * 4];
			surface[0] = 0;
			for (int k = 0; k < b->n; k++)
				strncat(surface, b->nodes[k].word,
					sizeof(surface) - strlen(surface) - 1);
			int ids[CTX_MAX_TOK];
			int nt = morph_tokenize(m, surface, ids, CTX_MAX_TOK - 1);
			if (nt < 1) continue;
			if (nt > CTX_MAX_TOK - 1) nt = CTX_MAX_TOK - 1;
			path_tok[i][0] = TOK_BOS;
			for (int k = 0; k < nt; k++) {
				path_tok[i][k + 1] = ids[k];
				path_tgt[i][k] = ids[k];
			}
			path_tgt[i][nt] = TOK_EOS;
			path_t[i] = nt + 1;
			order[n_paths++] = i;
		}
		/* Phase 2: score in token order so neighbours share long prefixes. */
		for (int a = 1; a < n_paths; a++) {          /* insertion sort, n <= 40 */
			int v = order[a], q = a - 1;
			while (q >= 0 && tok_seq_cmp(path_tok[order[q]], path_t[order[q]],
			                             path_tok[v], path_t[v]) > 0) {
				order[q + 1] = order[q]; q--;
			}
			order[q + 1] = v;
		}
		CtxCache cache;
		int cache_ok = g_lm_cache && ctx_cache_init(m, &cache) == 0;
		for (int a = 0; a < n_paths; a++) {
			int i = order[a];
			lm_cost[i] = cache_ok
				? ctx_path_cost_cached(m, &cache, path_tok[i], path_tgt[i], (uint32_t)path_t[i])
				: ctx_path_cost(m, path_tok[i], path_tgt[i], (uint32_t)path_t[i]);
		}
		if (cache_ok) ctx_cache_free(&cache);
		/* Phase 3: combine with lattice cost, symbol penalty and learned bonus. */
		for (int i = 0; i < n_rescore; i++) {
			Beam *b = &beams[nchars][i];
			if (b->n == 0) continue;
			float c = b->cost;
			if (path_t[i] > 0) {
				c += lm_cost[i];
				/* Emoticons / symbols (form -2) are out-of-vocabulary for the LM, so their
				 * LM cost is unrealistically low; keep them as candidates, not winners. */
				for (int k = 0; k < b->n; k++)
					if (b->nodes[k].form == SYMBOL_FORM) c += SYMBOL_LM_PENALTY;
				for (int k = 0; k < b->n; k++)
					c += m->ctx_scale * UNK_FOREIGN_NLL * (float)foreign_char_count(m, b->nodes[k].word);
				/* The learned bonus is already in b->cost (lexical_cost), so it is
				 * applied exactly once; adding it again here let a handful of
				 * commits (して→仕手 x5) outweigh the LM by ~10000. */
				if (g_verbose)
					fprintf(stderr, "  nbest[%d] local=%.1f total=%.1f\n", i, b->cost, c);
			}
			if (c < best_c) { best_c = c; best_i = i; }
		}
	}
	Beam *best = lm_best ? lm_best : &beams[nchars][best_i];
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
	free(lm_best);
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
	WhizDic *d, WhizModel *m, const char *reading,
	char *out, int outsz, int max_cands)
{
	if (max_cands <= 0 || max_cands > 256) max_cands = 20;
	char kata[MAX_READ_KEY];
	hira_to_kata(reading, kata, sizeof(kata));
	int chars = utf8_nchars(kata);
	if (chars <= 0) return 0;

	Node raw[512];
	int nraw = collect_cands_at(d, m, kata, reading, 0, chars, raw, 512);
	/* keep only candidates that span the whole reading (exact-reading match) */
	Node nodes[512];
	int count = 0;
	for (int i = 0; i < nraw; i++)
		if (raw[i].end == chars) nodes[count++] = raw[i];
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
static int predict_fallback(WhizDic *d, WhizModel *m, const char *yomi_prefix,
                            char *out, int outsz, int max_cands);

static int predict_convert(WhizDic *d, WhizModel *m, const char *yomi_prefix,
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
		if (bad_surface(e->read, e->word, e->form)) continue;
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
	/* No dictionary word starts with this reading (かんじへ): carry the
	 * prediction on from the converted text instead of going blank. */
	if (nc == 0) return predict_fallback(d, m, yomi_prefix, out, outsz, max_cands);
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

/* Appends "word\tread\n" to out unless `word` is already listed. */
static int pred_emit(char *out, int outsz, int *o, const char *word, const char *read)
{
	for (const char *p = out; p < out + *o; ) {
		const char *nl = memchr(p, '\n', (size_t)(out + *o - p));
		const char *end = nl ? nl : out + *o;
		const char *tab = memchr(p, '\t', (size_t)(end - p));
		if (tab && (size_t)(tab - p) == strlen(word) && !memcmp(p, word, (size_t)(tab - p)))
			return 0;
		p = end + 1;
	}
	int n = snprintf(out + *o, (size_t)(outsz - *o), "%s\t%s\n", word, read);
	if (n < 0 || n >= outsz - *o) return -1;
	*o += n;
	return 1;
}

/* One conversion split into segments: seg_word[i] / seg_read[i]. */
#define PRED_SEGS 32
typedef struct {
	int  n;
	char word[PRED_SEGS][MAX_WORD];
	char read[PRED_SEGS][MAX_READ_KEY];
} PredSegs;

static void pred_segs_parse(PredSegs *ps, const char *raw)
{
	ps->n = 0;
	for (const char *p = raw; *p && ps->n < PRED_SEGS; ) {
		const char *nl = strchr(p, '\n');
		const char *end = nl ? nl : p + strlen(p);
		const char *tab = memchr(p, '\t', (size_t)(end - p));
		if (tab) {
			int wn = (int)(tab - p), rn = (int)(end - tab - 1);
			if (wn >= MAX_WORD) wn = MAX_WORD - 1;
			if (rn >= MAX_READ_KEY) rn = MAX_READ_KEY - 1;
			memcpy(ps->word[ps->n], p, (size_t)wn); ps->word[ps->n][wn] = 0;
			memcpy(ps->read[ps->n], tab + 1, (size_t)rn); ps->read[ps->n][rn] = 0;
			ps->n++;
		}
		if (!nl) break;
		p = nl + 1;
	}
}

static void pred_segs_join(const PredSegs *ps, int replace, const char *with,
                           char *dst, int dstsz)
{
	int o = 0;
	dst[0] = 0;
	for (int i = 0; i < ps->n && o < dstsz - 1; i++)
		o += snprintf(dst + o, (size_t)(dstsz - o), "%s", i == replace ? with : ps->word[i]);
	if (o >= dstsz) dst[dstsz - 1] = 0;
}

/*
 * predict_fallback — 辞書に前方一致する語がない読み（かんじへ）の予測
 *   1. 読み全体の最良変換（漢字へ）
 *   2. 先頭側を変換し、残りの読みを前方一致で補った候補（漢字＋へ…）
 *   3. 文節ごとの別表記に差し替えた候補（感じへ）
 */
static int predict_fallback(WhizDic *d, WhizModel *m, const char *yomi_prefix,
                            char *out, int outsz, int max_cands)
{
	char hira[MAX_READ_KEY];
	kata_to_hira(yomi_prefix, hira, sizeof(hira));
	int nchars = utf8_nchars(hira);
	if (nchars < 2) return 0;

	char whole[1024], segraw[4096];
	if (convert(d, m, hira, whole, (int)sizeof(whole), 32, segraw, (int)sizeof(segraw)) <= 0)
		return 0;
	int o = 0, emitted = 0;
	if (pred_emit(out, outsz, &o, whole, hira) > 0) emitted++;

	/* head converted + tail completed from the dictionary */
	for (int k = nchars - 1; k >= 1 && emitted < max_cands; k--) {
		char head[MAX_READ_KEY], tail[MAX_READ_KEY], head_conv[1024], dummy[8];
		utf8_slice(hira, 0, k, head, sizeof(head));
		utf8_slice(hira, k, nchars - k, tail, sizeof(tail));
		char tail_kata[MAX_READ_KEY];
		hira_to_kata(tail, tail_kata, sizeof(tail_kata));
		int plen = (int)strlen(tail_kata);
		int lo = 0, hi = d->n;
		while (lo < hi) {
			int mid = (lo + hi) / 2;
			if (strncmp(d->entries[d->order[mid]].read, tail_kata, (size_t)plen) < 0) lo = mid + 1;
			else hi = mid;
		}
		PredCand cands[256];
		int nc = 0;
		for (int i = lo; i < d->n && nc < 256; i++) {
			DicEntry *e = &d->entries[d->order[i]];
			if (strncmp(e->read, tail_kata, (size_t)plen) != 0) break;
			if (bad_surface(e->read, e->word, e->form)) continue;
			/* skip the bare tail itself; it is already part of the whole conversion */
			if (strlen(e->read) == (size_t)plen) continue;
			int dup = 0;
			for (int j = 0; j < nc; j++)
				if (!strcmp(cands[j].word, e->word)) { dup = 1; break; }
			if (!dup) {
				cands[nc].word = e->word; cands[nc].read = e->read; cands[nc].cost = e->cost;
				nc++;
			}
		}
		if (nc == 0) continue;
		if (convert(d, m, head, head_conv, (int)sizeof(head_conv), 32, dummy, 0) <= 0) continue;
		qsort(cands, (size_t)nc, sizeof(PredCand), pred_cand_cmp);
		for (int i = 0; i < nc && i < 3 && emitted < max_cands; i++) {
			char word[1024], full[MAX_READ_KEY * 2], tail_read[MAX_READ_KEY];
			snprintf(word, sizeof(word), "%s%s", head_conv, cands[i].word);
			kata_to_hira(cands[i].read, tail_read, sizeof(tail_read));
			snprintf(full, sizeof(full), "%s%s", head, tail_read);
			int r = pred_emit(out, outsz, &o, word, full);
			if (r < 0) goto done;
			if (r > 0) emitted++;
		}
		break;	/* the longest convertible head with a completable tail is enough */
	}

	/* other spellings of one segment, rest of the sentence unchanged */
	{
		PredSegs ps;
		pred_segs_parse(&ps, segraw);
		for (int s = 0; s < ps.n && emitted < max_cands; s++) {
			char alts[2048];
			if (segment_candidates(d, m, ps.read[s], alts, (int)sizeof(alts), 4) <= 0) continue;
			for (char *p = alts; p && *p && emitted < max_cands; ) {
				char *nl = strchr(p, '\n');
				if (nl) *nl = 0;
				if (strcmp(p, ps.word[s])) {
					char word[1024];
					pred_segs_join(&ps, s, p, word, (int)sizeof(word));
					int r = pred_emit(out, outsz, &o, word, hira);
					if (r < 0) goto done;
					if (r > 0) emitted++;
				}
				p = nl ? nl + 1 : NULL;
			}
		}
	}
done:
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

static WhizModel g_net;
static WhizDic    g_dic;
static int        g_ready = 0;
static char       g_out[1024];
static char       g_aux_out[4096]; /* reverse / predict output */

WHIZ_API void        whiz_free(void);
WHIZ_API void        whiz_set_context(int on);
WHIZ_API void        whiz_set_search(int mode);
WHIZ_API const char *whiz_reverse(const char *word);
WHIZ_API const char *whiz_predict(const char *yomi_prefix, int max_cands);
WHIZ_API const char *whiz_convert_segments(const char *yomi);
WHIZ_API const char *whiz_segment_candidates(const char *reading, int max_cands);

/* whiz_learn — remember that `word` was chosen for `reading` (hiragana).
 * Persists to $WHIZ_LEARN, else ~/.local/share/whiz/learn.txt (WHIZ_LEARN="" disables). */
WHIZ_API
void whiz_learn(const char *reading, const char *word)
{
	if (!reading || !word || !*reading || !*word || !strcmp(reading, word)) return;
	if (strlen(reading) >= MAX_READ_KEY || strlen(word) >= MAX_WORD) return;
	learn_note(reading, word);
#ifndef __EMSCRIPTEN__
	if (!g_learn_path[0]) return;
	FILE *f = fopen(g_learn_path, "a");
	if (f) { fprintf(f, "%s\t%s\n", reading, word); fclose(f); }
#endif
}

#ifndef __EMSCRIPTEN__
#include <sys/stat.h>
static void whiz_learn_load(void)
{
	for (int i = 0; i < LEARN_SLOTS; i++) { free(g_learn[i].read); free(g_learn[i].word); }
	memset(g_learn, 0, sizeof(g_learn));
	g_learn_n = 0;
	const char *env = getenv("WHIZ_LEARN");
	if (env && !*env) return;
	if (env) snprintf(g_learn_path, sizeof(g_learn_path), "%s", env);
	else {
		const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
		char dir[480];
		if (xdg && *xdg) snprintf(dir, sizeof(dir), "%.400s/whiz", xdg);
		else if (home && *home) {
			char parent[480];
			snprintf(parent, sizeof(parent), "%.400s/.local/share", home);
			mkdir(parent, 0700);
			snprintf(dir, sizeof(dir), "%.400s/whiz", parent);
		} else return;
		mkdir(dir, 0700);
		snprintf(g_learn_path, sizeof(g_learn_path), "%s/learn.txt", dir);
	}
	FILE *f = fopen(g_learn_path, "r");
	if (!f) return;
	char line[512];
	while (fgets(line, sizeof line, f)) {
		char *tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = 0;
		char *w = tab + 1;
		w[strcspn(w, "\r\n")] = 0;
		if (*line && *w && strlen(line) < MAX_READ_KEY && strlen(w) < MAX_WORD) learn_note(line, w);
	}
	fclose(f);
}
#endif

WHIZ_API
int whiz_init(uint8_t *model, int model_sz, uint8_t *dic, int dic_sz)
{
	if (g_ready) whiz_free();
	if (wcnn_load_mem(model, (size_t)model_sz, &g_net) != 0) return 0;
	if (whiz_load_mem(dic, (size_t)dic_sz, &g_dic) != 0) {
		wcnn_free(&g_net);
		return 0;
	}
	g_ready = 1;
#ifndef __EMSCRIPTEN__
	whiz_learn_load();
#endif
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
 * whiz_set_context — stage 3（文脈 Transformer）の on/off
 * 同音異義語の精度は上がるが、候補ごとに語彙全体の softmax を計算するため
 * 遅くなる。低遅延を優先する場合は 0 を渡す。
 */
WHIZ_API void whiz_set_context(int on) { g_ctx_enabled = on ? 1 : 0; }
/* 1 = anchored LM beam (default, matches whiz.py), 0 = N-best rescoring (original) */
WHIZ_API void whiz_set_search(int mode) { g_search = mode ? 1 : 0; }

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
	int n = predict_convert(&g_dic, &g_net, yomi_prefix, g_aux_out,
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
		"Usage: %s -m model.wcnn -d whiz.web.dic -p \"よみ\" [-b beam] [-c 0|1] [-s 0|1] [-v]\n"
		"  -c 0  skip stage 3 (context Transformer): faster, weaker on homophones\n"
		"  python3 whiz.py train --preset tiny\n"
		"  python3 whiz.py export-web   # → out/model.wcnn + out/whiz.web.dic\n",
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
		else if (!strcmp(argv[i], "-c") && i + 1 < argc) g_ctx_enabled = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-s") && i + 1 < argc) g_search = atoi(argv[++i]) ? 1 : 0;
		else if (!strcmp(argv[i], "-v")) g_verbose = 1;
		else { usage(argv[0]); return 1; }
	}
	if (!model_path || !dic_path || !prompt) {
		usage(argv[0]);
		return 1;
	}

	WhizModel model;
	if (wcnn_load(model_path, &model) != 0) return 1;

	WhizDic dic;
	if (whiz_load(dic_path, &dic) != 0) {
		wcnn_free(&model);
		return 1;
	}
	if (0 /* v5: no n_words check */) {
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
