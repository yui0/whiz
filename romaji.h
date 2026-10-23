/*
 * Copyright © 2026 Yuichiro Nakada / Project Whiz
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Shared input and segmented-conversion helpers for every Whiz frontend.
 * Romaji rules are based on the legacy whiz_romakana_typing_rule table.
 */

#ifndef WHIZ_ROMAJI_H
#define WHIZ_ROMAJI_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

typedef struct {
	const char *romaji;
	const char *hiragana;
} WhizRomajiRule;

static const WhizRomajiRule whiz_romaji_rules[] = {
	{"kya","\u304d\u3083"},{"kyi","\u304d\u3043"},{"kyu","\u304d\u3085"},{"kye","\u304d\u3047"},{"kyo","\u304d\u3087"},
	{"gya","\u304e\u3083"},{"gyi","\u304e\u3043"},{"gyu","\u304e\u3085"},{"gye","\u304e\u3047"},{"gyo","\u304e\u3087"},
	{"sya","\u3057\u3083"},{"syi","\u3057\u3043"},{"syu","\u3057\u3085"},{"sye","\u3057\u3047"},{"syo","\u3057\u3087"},
	{"sha","\u3057\u3083"},{"shi","\u3057"},{"shu","\u3057\u3085"},{"she","\u3057\u3047"},{"sho","\u3057\u3087"},
	{"zya","\u3058\u3083"},{"zyi","\u3058\u3043"},{"zyu","\u3058\u3085"},{"zye","\u3058\u3047"},{"zyo","\u3058\u3087"},
	{"jya","\u3058\u3083"},{"jyi","\u3058\u3043"},{"jyu","\u3058\u3085"},{"jye","\u3058\u3047"},{"jyo","\u3058\u3087"},
	{"tya","\u3061\u3083"},{"tyi","\u3061\u3043"},{"tyu","\u3061\u3085"},{"tye","\u3061\u3047"},{"tyo","\u3061\u3087"},
	{"cha","\u3061\u3083"},{"chi","\u3061"},{"chu","\u3061\u3085"},{"che","\u3061\u3047"},{"cho","\u3061\u3087"},
	{"cya","\u3061\u3083"},{"cyi","\u3061\u3043"},{"cyu","\u3061\u3085"},{"cye","\u3061\u3047"},{"cyo","\u3061\u3087"},
	{"dya","\u3062\u3083"},{"dyi","\u3062\u3043"},{"dyu","\u3062\u3085"},{"dye","\u3062\u3047"},{"dyo","\u3062\u3087"},
	{"tha","\u3066\u3083"},{"thi","\u3066\u3043"},{"thu","\u3066\u3085"},{"the","\u3066\u3047"},{"tho","\u3066\u3087"},
	{"dha","\u3067\u3083"},{"dhi","\u3067\u3043"},{"dhu","\u3067\u3085"},{"dhe","\u3067\u3047"},{"dho","\u3067\u3087"},
	{"nya","\u306b\u3083"},{"nyi","\u306b\u3043"},{"nyu","\u306b\u3085"},{"nye","\u306b\u3047"},{"nyo","\u306b\u3087"},
	{"hya","\u3072\u3083"},{"hyi","\u3072\u3043"},{"hyu","\u3072\u3085"},{"hye","\u3072\u3047"},{"hyo","\u3072\u3087"},
	{"bya","\u3073\u3083"},{"byi","\u3073\u3043"},{"byu","\u3073\u3085"},{"bye","\u3073\u3047"},{"byo","\u3073\u3087"},
	{"pya","\u3074\u3083"},{"pyi","\u3074\u3043"},{"pyu","\u3074\u3085"},{"pye","\u3074\u3047"},{"pyo","\u3074\u3087"},
	{"mya","\u307f\u3083"},{"myi","\u307f\u3043"},{"myu","\u307f\u3085"},{"mye","\u307f\u3047"},{"myo","\u307f\u3087"},
	{"rya","\u308a\u3083"},{"ryi","\u308a\u3043"},{"ryu","\u308a\u3085"},{"rye","\u308a\u3047"},{"ryo","\u308a\u3087"},
	{"fya","\u3075\u3083"},{"fyi","\u3075\u3043"},{"fyu","\u3075\u3085"},{"fye","\u3075\u3047"},{"fyo","\u3075\u3087"},
	{"tsa","\u3064\u3041"},{"tsi","\u3064\u3043"},{"tse","\u3064\u3047"},{"tso","\u3064\u3049"},
	{"wha","\u3046\u3041"},{"whi","\u3046\u3043"},{"whe","\u3046\u3047"},{"who","\u3046\u3049"},
	{"ltsu","\u3063"},{"xtsu","\u3063"},{"ltu","\u3063"},{"xtu","\u3063"},
	{"xya","\u3083"},{"xyu","\u3085"},{"xyo","\u3087"},{"lya","\u3083"},{"lyu","\u3085"},{"lyo","\u3087"},
	{"xwa","\u308e"},{"lka","\u30f5"},{"xka","\u30f5"},{"lke","\u30f6"},{"xke","\u30f6"},
	{"va","\u3094\u3041"},{"vi","\u3094\u3043"},{"vu","\u3094"},{"ve","\u3094\u3047"},{"vo","\u3094\u3049"},
	{"fa","\u3075\u3041"},{"fi","\u3075\u3043"},{"fu","\u3075"},{"fe","\u3075\u3047"},{"fo","\u3075\u3049"},
	{"ja","\u3058\u3083"},{"ji","\u3058"},{"ju","\u3058\u3085"},{"je","\u3058\u3047"},{"jo","\u3058\u3087"},
	{"xa","\u3041"},{"xi","\u3043"},{"xu","\u3045"},{"xe","\u3047"},{"xo","\u3049"},
	{"la","\u3041"},{"li","\u3043"},{"lu","\u3045"},{"le","\u3047"},{"lo","\u3049"},
	{"ka","\u304b"},{"ki","\u304d"},{"ku","\u304f"},{"ke","\u3051"},{"ko","\u3053"},
	{"ga","\u304c"},{"gi","\u304e"},{"gu","\u3050"},{"ge","\u3052"},{"go","\u3054"},
	{"sa","\u3055"},{"si","\u3057"},{"su","\u3059"},{"se","\u305b"},{"so","\u305d"},
	{"za","\u3056"},{"zi","\u3058"},{"zu","\u305a"},{"ze","\u305c"},{"zo","\u305e"},
	{"ta","\u305f"},{"ti","\u3061"},{"tu","\u3064"},{"tsu","\u3064"},{"te","\u3066"},{"to","\u3068"},
	{"da","\u3060"},{"di","\u3062"},{"du","\u3065"},{"de","\u3067"},{"do","\u3069"},
	{"na","\u306a"},{"ni","\u306b"},{"nu","\u306c"},{"ne","\u306d"},{"no","\u306e"},
	{"ha","\u306f"},{"hi","\u3072"},{"hu","\u3075"},{"he","\u3078"},{"ho","\u307b"},
	{"ba","\u3070"},{"bi","\u3073"},{"bu","\u3076"},{"be","\u3079"},{"bo","\u307c"},
	{"pa","\u3071"},{"pi","\u3074"},{"pu","\u3077"},{"pe","\u307a"},{"po","\u307d"},
	{"ma","\u307e"},{"mi","\u307f"},{"mu","\u3080"},{"me","\u3081"},{"mo","\u3082"},
	{"ya","\u3084"},{"yu","\u3086"},{"yo","\u3088"},
	{"ra","\u3089"},{"ri","\u308a"},{"ru","\u308b"},{"re","\u308c"},{"ro","\u308d"},
	{"wa","\u308f"},{"wi","\u3046\u3043"},{"we","\u3046\u3047"},{"wo","\u3092"},
	{"a","\u3042"},{"i","\u3044"},{"u","\u3046"},{"e","\u3048"},{"o","\u304a"},
	{"-","\u30fc"},{".","\u3002"},{",","\u3001"},{"[","\u300c"},{"]","\u300d"},{"/","\u30fb"},
	{NULL, NULL}
};

static inline bool whiz_romaji_append(char *out, size_t cap, size_t *used,
	const char *text, size_t bytes)
{
	if (*used + bytes >= cap) return false;
	memcpy(out + *used, text, bytes);
	*used += bytes;
	out[*used] = '\0';
	return true;
}

static inline bool whiz_romaji_consonant(char c)
{
	return c >= 'a' && c <= 'z' && !strchr("aeioun", c);
}

/* final_n=false keeps a trailing n pending; true resolves it. */
static inline void whiz_romaji_convert(const char *input, char *out, size_t cap,
	bool final_n)
{
	size_t i = 0, used = 0;
	if (!cap) return;
	out[0] = '\0';
	while (input[i]) {
		unsigned char byte = (unsigned char)input[i];
		if (byte >= 0x80) {
			size_t bytes = (byte & 0xf0U) == 0xf0U ? 4 :
				((byte & 0xe0U) == 0xe0U ? 3 : 2);
			if (!whiz_romaji_append(out, cap, &used, input + i, bytes)) break;
			i += bytes;
			continue;
		}

		char c = input[i];
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		char next = input[i + 1];
		if (next >= 'A' && next <= 'Z') next = (char)(next - 'A' + 'a');

		if (whiz_romaji_consonant(c) && next == c) {
			if (!whiz_romaji_append(out, cap, &used, "\u3063", strlen("\u3063"))) break;
			i++;
			continue;
		}
		if (c == 'n') {
			if (next == '\'') {
				/* n' → ん (2 chars). Bare ' is a separator only. */
				if (!whiz_romaji_append(out, cap, &used, "\u3093", strlen("\u3093"))) break;
				i += 2;
				continue;
			}
			if (next == 'n') {
				/* nn → ん always (consume both). Vowels after stay independent:
				 * hanni→はんい, nna→んあ. んな is nnna / n'na; こんにちは is konnnichiha. */
				if (!whiz_romaji_append(out, cap, &used, "\u3093", strlen("\u3093"))) break;
				i += 2;
				continue;
			}
			if (next == 'x') {
				/* nx → ん (Sharp / some IME terminator; hanxi→はんい) */
				if (!whiz_romaji_append(out, cap, &used, "\u3093", strlen("\u3093"))) break;
				i += 2;
				continue;
			}
			if ((next && next != 'y' && !strchr("aeiou", next)) ||
			    (!next && final_n)) {
				/* n before consonant, or trailing n (final_n=true) → ん */
				if (!whiz_romaji_append(out, cap, &used, "\u3093", strlen("\u3093"))) break;
				i++;
				continue;
			}
		}
		/* xn → ん (Mozc alternative to nn) */
		if (c == 'x' && next == 'n') {
			if (!whiz_romaji_append(out, cap, &used, "\u3093", strlen("\u3093"))) break;
			i += 2;
			continue;
		}
		/* Lone apostrophe is only a romaji separator; skip it. */
		if (c == '\'') {
			i++;
			continue;
		}

		const WhizRomajiRule *best = NULL;
		size_t best_len = 0;
		for (const WhizRomajiRule *rule = whiz_romaji_rules;
		     rule->romaji; rule++) {
			size_t len = strlen(rule->romaji);
			if (len > best_len && strncasecmp(input + i, rule->romaji, len) == 0) {
				best = rule;
				best_len = len;
			}
		}
		if (best) {
			if (!whiz_romaji_append(out, cap, &used, best->hiragana,
			                        strlen(best->hiragana))) break;
			i += best_len;
			continue;
		}
		if (!whiz_romaji_append(out, cap, &used, &c, 1)) break;
		i++;
	}
}

static inline bool whiz_romaji_backspace(char *input)
{
	size_t n = strlen(input);
	if (!n) return false;
	size_t p = n - 1;
	while (p && (((unsigned char)input[p] & 0xc0U) == 0x80U)) p--;
	input[p] = '\0';
	return true;
}

/* ローマ字バッファの末尾から 1 かな単位分を削除する。
 * preedit の末尾がかなバイト(>= 0x80)になるまでASCII文字を1つずつ取り除く。
 * 例: "kashi"→"し" 3文字分消費して "か"、"ka"→"" 2文字消費で空。
 * out には削除後の変換結果が書かれる。true を返せば何か削除した。 */
static inline bool whiz_romaji_backspace_kana(char *input, char *out, size_t cap)
{
	size_t n = strlen(input);
	if (!n) { if (cap) out[0] = '\0'; return false; }
	do {
		input[--n] = '\0';
		whiz_romaji_convert(input, out, cap, false);
		if (!n) return true;
		size_t olen = strlen(out);
		/* preedit の末尾がかな(マルチバイト末端バイト >= 0x80)なら完了 */
		if (olen && (unsigned char)out[olen - 1] >= 0x80) return true;
	} while (n);
	return true;
}

/*
 * Shared UTF-8 kana / alphanumeric transforms for Whiz F6–F10 keys.
 *   F6  → ひらがな
 *   F7  → カタカナ
 *   F8  → ｶﾀｶﾅ
 *   F9  → ｋａｔａｋａｎａ (full-width latin)
 *   F10 → katakana (half-width latin)
 */

#include <stdint.h>

enum {
	WHIZ_XFORM_HIRAGANA = 6,
	WHIZ_XFORM_KATAKANA = 7,
	WHIZ_XFORM_HW_KATA  = 8,
	WHIZ_XFORM_FW_ASCII = 9,
	WHIZ_XFORM_HW_ASCII = 10
};

static inline bool whiz_xform_append(char *out, size_t cap, size_t *used,
	const char *text, size_t bytes)
{
	if (*used + bytes >= cap) return false;
	memcpy(out + *used, text, bytes);
	*used += bytes;
	out[*used] = '\0';
	return true;
}

static inline bool whiz_xform_put_cp(char *out, size_t cap, size_t *used,
	uint32_t cp)
{
	char buf[4];
	size_t n;
	if (cp < 0x80U) {
		buf[0] = (char)cp;
		n = 1;
	} else if (cp < 0x800U) {
		buf[0] = (char)(0xc0U | (cp >> 6));
		buf[1] = (char)(0x80U | (cp & 0x3fU));
		n = 2;
	} else if (cp < 0x10000U) {
		buf[0] = (char)(0xe0U | (cp >> 12));
		buf[1] = (char)(0x80U | ((cp >> 6) & 0x3fU));
		buf[2] = (char)(0x80U | (cp & 0x3fU));
		n = 3;
	} else {
		buf[0] = (char)(0xf0U | (cp >> 18));
		buf[1] = (char)(0x80U | ((cp >> 12) & 0x3fU));
		buf[2] = (char)(0x80U | ((cp >> 6) & 0x3fU));
		buf[3] = (char)(0x80U | (cp & 0x3fU));
		n = 4;
	}
	return whiz_xform_append(out, cap, used, buf, n);
}

static inline uint32_t whiz_xform_decode(const char *s, size_t *adv)
{
	unsigned char c0 = (unsigned char)s[0];
	if (c0 < 0x80U) {
		*adv = 1;
		return c0;
	}
	if ((c0 & 0xe0U) == 0xc0U && (unsigned char)s[1]) {
		*adv = 2;
		return ((uint32_t)(c0 & 0x1fU) << 6) |
		       ((uint32_t)((unsigned char)s[1] & 0x3fU));
	}
	if ((c0 & 0xf0U) == 0xe0U && (unsigned char)s[1] && (unsigned char)s[2]) {
		*adv = 3;
		return ((uint32_t)(c0 & 0x0fU) << 12) |
		       ((uint32_t)((unsigned char)s[1] & 0x3fU) << 6) |
		       ((uint32_t)((unsigned char)s[2] & 0x3fU));
	}
	if ((c0 & 0xf8U) == 0xf0U && (unsigned char)s[1] &&
	    (unsigned char)s[2] && (unsigned char)s[3]) {
		*adv = 4;
		return ((uint32_t)(c0 & 0x07U) << 18) |
		       ((uint32_t)((unsigned char)s[1] & 0x3fU) << 12) |
		       ((uint32_t)((unsigned char)s[2] & 0x3fU) << 6) |
		       ((uint32_t)((unsigned char)s[3] & 0x3fU));
	}
	*adv = 1;
	return c0;
}

/*
 * Half-width for U+30A1..U+30F3 in Unicode order
 * (ァアィイゥウ… interleaved; voiced = base + dakuten).
 */
static const char *const whiz_hw_kata_base[] = {
	/* 30A1 ァ */ "\xef\xbd\xa7", /* 30A2 ア */ "\xef\xbd\xb1",
	/* 30A3 ィ */ "\xef\xbd\xa8", /* 30A4 イ */ "\xef\xbd\xb2",
	/* 30A5 ゥ */ "\xef\xbd\xa9", /* 30A6 ウ */ "\xef\xbd\xb3",
	/* 30A7 ェ */ "\xef\xbd\xaa", /* 30A8 エ */ "\xef\xbd\xb4",
	/* 30A9 ォ */ "\xef\xbd\xab", /* 30AA オ */ "\xef\xbd\xb5",
	/* 30AB カ */ "\xef\xbd\xb6", /* 30AC ガ */ "\xef\xbd\xb6\xef\xbe\x9e",
	/* 30AD キ */ "\xef\xbd\xb7", /* 30AE ギ */ "\xef\xbd\xb7\xef\xbe\x9e",
	/* 30AF ク */ "\xef\xbd\xb8", /* 30B0 グ */ "\xef\xbd\xb8\xef\xbe\x9e",
	/* 30B1 ケ */ "\xef\xbd\xb9", /* 30B2 ゲ */ "\xef\xbd\xb9\xef\xbe\x9e",
	/* 30B3 コ */ "\xef\xbd\xba", /* 30B4 ゴ */ "\xef\xbd\xba\xef\xbe\x9e",
	/* 30B5 サ */ "\xef\xbd\xbb", /* 30B6 ザ */ "\xef\xbd\xbb\xef\xbe\x9e",
	/* 30B7 シ */ "\xef\xbd\xbc", /* 30B8 ジ */ "\xef\xbd\xbc\xef\xbe\x9e",
	/* 30B9 ス */ "\xef\xbd\xbd", /* 30BA ズ */ "\xef\xbd\xbd\xef\xbe\x9e",
	/* 30BB セ */ "\xef\xbd\xbe", /* 30BC ゼ */ "\xef\xbd\xbe\xef\xbe\x9e",
	/* 30BD ソ */ "\xef\xbd\xbf", /* 30BE ゾ */ "\xef\xbd\xbf\xef\xbe\x9e",
	/* 30BF タ */ "\xef\xbe\x80", /* 30C0 ダ */ "\xef\xbe\x80\xef\xbe\x9e",
	/* 30C1 チ */ "\xef\xbe\x81", /* 30C2 ヂ */ "\xef\xbe\x81\xef\xbe\x9e",
	/* 30C3 ッ */ "\xef\xbd\xaf", /* 30C4 ツ */ "\xef\xbe\x82",
	/* 30C5 ヅ */ "\xef\xbe\x82\xef\xbe\x9e",
	/* 30C6 テ */ "\xef\xbe\x83", /* 30C7 デ */ "\xef\xbe\x83\xef\xbe\x9e",
	/* 30C8 ト */ "\xef\xbe\x84", /* 30C9 ド */ "\xef\xbe\x84\xef\xbe\x9e",
	/* 30CA ナ */ "\xef\xbe\x85", /* 30CB ニ */ "\xef\xbe\x86",
	/* 30CC ヌ */ "\xef\xbe\x87", /* 30CD ネ */ "\xef\xbe\x88",
	/* 30CE ノ */ "\xef\xbe\x89",
	/* 30CF ハ */ "\xef\xbe\x8a", /* 30D0 バ */ "\xef\xbe\x8a\xef\xbe\x9e",
	/* 30D1 パ */ "\xef\xbe\x8a\xef\xbe\x9f",
	/* 30D2 ヒ */ "\xef\xbe\x8b", /* 30D3 ビ */ "\xef\xbe\x8b\xef\xbe\x9e",
	/* 30D4 ピ */ "\xef\xbe\x8b\xef\xbe\x9f",
	/* 30D5 フ */ "\xef\xbe\x8c", /* 30D6 ブ */ "\xef\xbe\x8c\xef\xbe\x9e",
	/* 30D7 プ */ "\xef\xbe\x8c\xef\xbe\x9f",
	/* 30D8 ヘ */ "\xef\xbe\x8d", /* 30D9 ベ */ "\xef\xbe\x8d\xef\xbe\x9e",
	/* 30DA ペ */ "\xef\xbe\x8d\xef\xbe\x9f",
	/* 30DB ホ */ "\xef\xbe\x8e", /* 30DC ボ */ "\xef\xbe\x8e\xef\xbe\x9e",
	/* 30DD ポ */ "\xef\xbe\x8e\xef\xbe\x9f",
	/* 30DE マ */ "\xef\xbe\x8f", /* 30DF ミ */ "\xef\xbe\x90",
	/* 30E0 ム */ "\xef\xbe\x91", /* 30E1 メ */ "\xef\xbe\x92",
	/* 30E2 モ */ "\xef\xbe\x93",
	/* 30E3 ャ */ "\xef\xbd\xac", /* 30E4 ヤ */ "\xef\xbe\x94",
	/* 30E5 ュ */ "\xef\xbd\xad", /* 30E6 ユ */ "\xef\xbe\x95",
	/* 30E7 ョ */ "\xef\xbd\xae", /* 30E8 ヨ */ "\xef\xbe\x96",
	/* 30E9 ラ */ "\xef\xbe\x97", /* 30EA リ */ "\xef\xbe\x98",
	/* 30EB ル */ "\xef\xbe\x99", /* 30EC レ */ "\xef\xbe\x9a",
	/* 30ED ロ */ "\xef\xbe\x9b",
	/* 30EE ヮ */ "\xef\xbe\x9c", /* 30EF ワ */ "\xef\xbe\x9c",
	/* 30F0 ヰ */ "\xef\xbd\xb2", /* 30F1 ヱ */ "\xef\xbd\xb4",
	/* 30F2 ヲ */ "\xef\xbd\xa6", /* 30F3 ン */ "\xef\xbe\x9d"
};
_Static_assert(sizeof(whiz_hw_kata_base) / sizeof(whiz_hw_kata_base[0]) == 83,
	"half-width katakana table must cover U+30A1..U+30F3");

static inline uint32_t whiz_xform_hw_to_fw_kata(uint32_t cp)
{
	/* U+FF66..U+FF9D → full-width katakana (dakuten/handakuten kept) */
	static const uint32_t hw2fw[] = {
		0x30f2,0x30a1,0x30a3,0x30a5,0x30a7,0x30a9,0x30e3,0x30e5,
		0x30e7,0x30c3,0x30fc,0x30a2,0x30a4,0x30a6,0x30a8,0x30aa,
		0x30ab,0x30ad,0x30af,0x30b1,0x30b3,0x30b5,0x30b7,0x30b9,
		0x30bb,0x30bd,0x30bf,0x30c1,0x30c4,0x30c6,0x30c8,0x30ca,
		0x30cb,0x30cc,0x30cd,0x30ce,0x30cf,0x30d2,0x30d5,0x30d8,
		0x30db,0x30de,0x30df,0x30e0,0x30e1,0x30e2,0x30e4,0x30e6,
		0x30e8,0x30e9,0x30ea,0x30eb,0x30ec,0x30ed,0x30ef,0x30f3,
		0x309b,0x309c
	};
	if (cp >= 0xff66U && cp <= 0xff9fU)
		return hw2fw[cp - 0xff66U];
	return cp;
}

static inline uint32_t whiz_xform_to_hiragana_cp(uint32_t cp)
{
	if (cp >= 0x30a1U && cp <= 0x30f6U) return cp - 0x60U;
	if (cp == 0x30fcU) return cp;
	if (cp >= 0xff66U && cp <= 0xff9fU) {
		uint32_t fw = whiz_xform_hw_to_fw_kata(cp);
		if (fw >= 0x30a1U && fw <= 0x30f6U) return fw - 0x60U;
		return fw;
	}
	if (cp >= 0xff01U && cp <= 0xff5eU) return cp - 0xfee0U;
	if (cp == 0x3000U) return ' ';
	return cp;
}

static inline uint32_t whiz_xform_to_katakana_cp(uint32_t cp)
{
	cp = whiz_xform_to_hiragana_cp(cp);
	if (cp >= 0x3041U && cp <= 0x3096U) return cp + 0x60U;
	return cp;
}

static inline bool whiz_xform_put_hw_kata(char *out, size_t cap, size_t *used,
	uint32_t cp)
{
	cp = whiz_xform_to_katakana_cp(cp);
	if (cp >= 0x30a1U && cp <= 0x30f3U) {
		const char *s = whiz_hw_kata_base[cp - 0x30a1U];
		return whiz_xform_append(out, cap, used, s, strlen(s));
	}
	if (cp == 0x30fcU) /* ー */
		return whiz_xform_append(out, cap, used, "\xef\xbd\xb0", 3);
	if (cp == 0x30fbU) /* ・ */
		return whiz_xform_append(out, cap, used, "\xef\xbd\xa5", 3);
	if (cp == 0x3001U) /* 、 */
		return whiz_xform_append(out, cap, used, "\xef\xbd\xa4", 3);
	if (cp == 0x3002U) /* 。 */
		return whiz_xform_append(out, cap, used, "\xef\xbd\xa1", 3);
	return whiz_xform_put_cp(out, cap, used, cp);
}

static inline bool whiz_xform_put_fw_ascii(char *out, size_t cap, size_t *used,
	uint32_t cp)
{
	if (cp == ' ') return whiz_xform_put_cp(out, cap, used, 0x3000U);
	if (cp >= 0x21U && cp <= 0x7eU)
		return whiz_xform_put_cp(out, cap, used, cp + 0xfee0U);
	return whiz_xform_put_cp(out, cap, used, cp);
}

static inline const char *whiz_xform_latin_source(const char *reading,
	const char *romaji)
{
	if (romaji && romaji[0]) return romaji;
	return reading ? reading : "";
}

/*
 * Apply F6–F10 style transform.
 * reading: current kana reading (UTF-8)
 * romaji:  original latin buffer (may be empty)
 */
static inline bool whiz_xform_apply(int mode, const char *reading,
	const char *romaji, char *out, size_t cap)
{
	if (!out || !cap) return false;
	out[0] = '\0';
	const char *src;
	size_t used = 0;

	switch (mode) {
	case WHIZ_XFORM_HW_ASCII:
		src = whiz_xform_latin_source(reading, romaji);
		if (!src[0]) return false;
		for (size_t i = 0; src[i]; ) {
			size_t adv = 0;
			uint32_t cp = whiz_xform_decode(src + i, &adv);
			cp = whiz_xform_to_hiragana_cp(cp);
			if (cp >= 0x80U) {
				if (!whiz_xform_put_cp(out, cap, &used, cp)) break;
			} else {
				char c = (char)cp;
				if (!whiz_xform_append(out, cap, &used, &c, 1)) break;
			}
			i += adv;
		}
		return used > 0;

	case WHIZ_XFORM_FW_ASCII:
		src = whiz_xform_latin_source(reading, romaji);
		if (!src[0]) return false;
		for (size_t i = 0; src[i]; ) {
			size_t adv = 0;
			uint32_t cp = whiz_xform_decode(src + i, &adv);
			cp = whiz_xform_to_hiragana_cp(cp);
			if (!whiz_xform_put_fw_ascii(out, cap, &used, cp)) break;
			i += adv;
		}
		return used > 0;

	case WHIZ_XFORM_HIRAGANA:
	case WHIZ_XFORM_KATAKANA:
	case WHIZ_XFORM_HW_KATA:
		src = (reading && reading[0]) ? reading :
		      (romaji && romaji[0] ? romaji : "");
		if (!src[0]) return false;
		for (size_t i = 0; src[i]; ) {
			size_t adv = 0;
			uint32_t cp = whiz_xform_decode(src + i, &adv);
			if (mode == WHIZ_XFORM_HIRAGANA) {
				if (!whiz_xform_put_cp(out, cap, &used,
				                       whiz_xform_to_hiragana_cp(cp)))
					break;
			} else if (mode == WHIZ_XFORM_KATAKANA) {
				if (!whiz_xform_put_cp(out, cap, &used,
				                       whiz_xform_to_katakana_cp(cp)))
					break;
			} else {
				if (!whiz_xform_put_hw_kata(out, cap, &used, cp)) break;
			}
			i += adv;
		}
		return used > 0;

	default:
		return false;
	}
}

/* Shared segmented conversion state and operations. */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define WHIZ_CONV_SEGMENTS 64
#define WHIZ_CONV_CANDIDATES 20
#define WHIZ_CONV_WORD 256
#define WHIZ_CONV_READING 256

typedef struct {
	char reading[WHIZ_CONV_READING];
	char candidates[WHIZ_CONV_CANDIDATES][WHIZ_CONV_WORD];
	int candidate_count;
	int selected;
} WhizConversionSegment;

typedef struct {
	WhizConversionSegment segments[WHIZ_CONV_SEGMENTS];
	int segment_count;
	int active;
} WhizConversion;

static inline void whiz_conv_clear(WhizConversion *conversion)
{
	memset(conversion, 0, sizeof(*conversion));
	conversion->active = -1;
}

static inline void whiz_conv_copy(char *dst, size_t cap, const char *src)
{
	if (!cap) return;
	if (!src) src = "";
	size_t n = strlen(src);
	if (n >= cap) n = cap - 1;
	memmove(dst, src, n);
	dst[n] = '\0';
}

static inline bool whiz_conv_has_candidate(
	const WhizConversionSegment *segment, const char *word)
{
	for (int i = 0; i < segment->candidate_count; i++)
		if (!strcmp(segment->candidates[i], word)) return true;
	return false;
}

static inline void whiz_conv_load_candidates(
	WhizConversionSegment *segment, const char *preferred)
{
	segment->candidate_count = 0;
	segment->selected = 0;
	if (preferred && *preferred) {
		whiz_conv_copy(segment->candidates[segment->candidate_count++],
		               WHIZ_CONV_WORD, preferred);
	}

	const char *raw_api = whiz_segment_candidates(
		segment->reading, WHIZ_CONV_CANDIDATES);
	char raw[4096];
	whiz_conv_copy(raw, sizeof(raw), raw_api);
	char *line = raw;
	while (*line && segment->candidate_count < WHIZ_CONV_CANDIDATES) {
		char *newline = strchr(line, '\n');
		if (newline) *newline = '\0';
		if (*line && !whiz_conv_has_candidate(segment, line))
			whiz_conv_copy(segment->candidates[segment->candidate_count++],
			               WHIZ_CONV_WORD, line);
		if (!newline) break;
		line = newline + 1;
	}
	if (!segment->candidate_count)
		whiz_conv_copy(segment->candidates[segment->candidate_count++],
		               WHIZ_CONV_WORD, segment->reading);
}

static inline bool whiz_conv_begin(WhizConversion *conversion, const char *reading)
{
	whiz_conv_clear(conversion);
	const char *raw_api = whiz_convert_segments(reading);
	if (!raw_api || !*raw_api) return false;
	char raw[4096];
	whiz_conv_copy(raw, sizeof(raw), raw_api);

	char *line = raw;
	while (*line && conversion->segment_count < WHIZ_CONV_SEGMENTS) {
		char *newline = strchr(line, '\n');
		if (newline) *newline = '\0';
		char *tab = strchr(line, '\t');
		if (tab && tab != line && tab[1]) {
			*tab = '\0';
			WhizConversionSegment *segment =
				&conversion->segments[conversion->segment_count++];
			whiz_conv_copy(segment->reading, sizeof(segment->reading), tab + 1);
			whiz_conv_load_candidates(segment, line);
		}
		if (!newline) break;
		line = newline + 1;
	}
	conversion->active = conversion->segment_count ? 0 : -1;
	return conversion->segment_count > 0;
}

static inline const char *whiz_conv_selected(const WhizConversionSegment *segment)
{
	return segment->candidate_count
		? segment->candidates[segment->selected] : segment->reading;
}

static inline void whiz_conv_compose(
	const WhizConversion *conversion, char *out, size_t cap)
{
	size_t used = 0;
	if (!cap) return;
	out[0] = '\0';
	for (int i = 0; i < conversion->segment_count; i++) {
		const char *word = whiz_conv_selected(&conversion->segments[i]);
		size_t n = strlen(word);
		if (n >= cap - used) n = cap - used - 1;
		memcpy(out + used, word, n);
		used += n;
		out[used] = '\0';
		if (used + 1 >= cap) break;
	}
}

/* Remember the committed choice of every segment (see whiz_learn in engine/whiz.c). */
static inline void whiz_conv_learn(const WhizConversion *conversion)
{
	for (int i = 0; i < conversion->segment_count; i++) {
		const WhizConversionSegment *segment = &conversion->segments[i];
		whiz_learn(segment->reading, whiz_conv_selected(segment));
	}
}

static inline void whiz_conv_select_segment(WhizConversion *conversion, int delta)
{
	if (!conversion->segment_count) return;
	conversion->active =
		(conversion->active + delta + conversion->segment_count)
		% conversion->segment_count;
}

static inline void whiz_conv_select_candidate(WhizConversion *conversion, int delta)
{
	if (conversion->active < 0) return;
	WhizConversionSegment *segment = &conversion->segments[conversion->active];
	if (!segment->candidate_count) return;
	segment->selected =
		(segment->selected + delta + segment->candidate_count)
		% segment->candidate_count;
}

static inline size_t whiz_conv_first_char_bytes(const char *text)
{
	unsigned char c = (unsigned char)text[0];
	if (c < 0x80) return c ? 1 : 0;
	if ((c & 0xe0) == 0xc0) return 2;
	if ((c & 0xf0) == 0xe0) return 3;
	if ((c & 0xf8) == 0xf0) return 4;
	return 1;
}

static inline size_t whiz_conv_last_char_offset(const char *text)
{
	size_t n = strlen(text);
	if (!n) return 0;
	size_t p = n - 1;
	while (p && ((unsigned char)text[p] & 0xc0) == 0x80) p--;
	return p;
}

/*
 * Resize the boundary after the active segment. Negative shortens it,
 * positive takes one character from the following segment.
 */
static inline bool whiz_conv_resize(WhizConversion *conversion, int delta)
{
	int i = conversion->active;
	if (i < 0 || i + 1 >= conversion->segment_count || !delta) return false;
	WhizConversionSegment *left = &conversion->segments[i];
	WhizConversionSegment *right = &conversion->segments[i + 1];
	char moved[8] = "";
	if (delta < 0) {
		size_t p = whiz_conv_last_char_offset(left->reading);
		if (!p) return false; /* Keep at least one character in each segment. */
		whiz_conv_copy(moved, sizeof(moved), left->reading + p);
		left->reading[p] = '\0';
		char joined[WHIZ_CONV_READING];
		snprintf(joined, sizeof(joined), "%s%s", moved, right->reading);
		whiz_conv_copy(right->reading, sizeof(right->reading), joined);
	} else {
		size_t n = whiz_conv_first_char_bytes(right->reading);
		if (!n || !right->reading[n]) return false;
		memcpy(moved, right->reading, n);
		moved[n] = '\0';
		if (strlen(left->reading) + n >= sizeof(left->reading)) return false;
		strcat(left->reading, moved);
		memmove(right->reading, right->reading + n,
		        strlen(right->reading + n) + 1);
	}
	const char *best = whiz_convert(left->reading);
	char left_best[WHIZ_CONV_WORD];
	whiz_conv_copy(left_best, sizeof(left_best), best);
	best = whiz_convert(right->reading);
	char right_best[WHIZ_CONV_WORD];
	whiz_conv_copy(right_best, sizeof(right_best), best);
	whiz_conv_load_candidates(left, left_best);
	whiz_conv_load_candidates(right, right_best);
	return true;
}


#endif
