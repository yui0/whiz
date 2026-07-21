/*
 * Shared UTF-8 romaji-to-hiragana conversion for every Whiz frontend.
 * The rules are based on the legacy whiz_romakana_typing_rule table.
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
			char after = input[i + 2];
			if (after >= 'A' && after <= 'Z') after = (char)(after - 'A' + 'a');
			if (next == '\'') {
				/* n' \u2192 \u3093 (2 chars) */
				if (!whiz_romaji_append(out, cap, &used, "\u3093", strlen("\u3093"))) break;
				i += 2;
				continue;
			}
			if (next == 'n') {
				/* "nn" \u306f\u5e38\u306b\u3093\u4e00\u6587\u5b57\u3002
				 * after \u304c\u6bcd\u97f3/y \u306a\u3089\u6b21\u306e n \u304b\u3089\u65b0\u97f3\u7bc0\u3092\u59cb\u3081\u308b\u305f\u3081 1 \u6587\u5b57\u6d88\u8cbb
				 * (\u4f8b: nna\u2192\u3093\u306a, nnya\u2192\u3093\u306b\u3083)\u3002
				 * \u305d\u308c\u4ee5\u5916(\u5b50\u97f3\u30fb\u672b\u5c3e)\u306f nn \u3092\u4e00\u62ec\u6d88\u8cbb\u3057\u30662\u6587\u5b57\u9032\u3080
				 * (\u4f8b: nnk\u2192\u3093+k, nn$\u2192\u3093)\u3002 */
				bool after_starts_syllable = after &&
					(strchr("aeiou", after) || after == 'y');
				if (!whiz_romaji_append(out, cap, &used, "\u3093", strlen("\u3093"))) break;
				i += after_starts_syllable ? 1 : 2;
				continue;
			}
			if ((next && next != 'y' && !strchr("aeiou", next)) ||
			    (!next && final_n)) {
				/* n \u306e\u76f4\u5f8c\u304c\u5b50\u97f3\u3001\u307e\u305f\u306f\u672b\u5c3e (final_n=true) \u2192 \u3093 */
				if (!whiz_romaji_append(out, cap, &used, "\u3093", strlen("\u3093"))) break;
				i++;
				continue;
			}
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

#endif
