#ifndef __WHIZ_CONV_TABLE_H__
#define __WHIZ_CONV_TABLE_H__

typedef struct _ConvRule
{
    const char *string;
    const char *result;
    const char *cont;
} ConvRule;

typedef struct _HiraganaKatakanaRule
{
    const char *hiragana;
    const char *katakana;
    const char *half_katakana;
} HiraganaKatakanaRule;

typedef struct _WideRule
{
    const char *code;
    const char *wide;
} WideRule;

extern ConvRule whiz_romakana_typing_rule[];
extern ConvRule whiz_kana_typing_rule[];

// symbol & number
extern ConvRule whiz_romakana_symbol_rule[];
extern ConvRule whiz_romakana_wide_symbol_rule[];
extern ConvRule whiz_romakana_number_rule[];
extern ConvRule whiz_romakana_wide_number_rule[];

// period rule
extern ConvRule whiz_romakana_ja_period_rule[];
extern ConvRule whiz_romakana_wide_latin_period_rule[];
extern ConvRule whiz_romakana_latin_period_rule[];

extern ConvRule whiz_kana_ja_period_rule[];
extern ConvRule whiz_kana_wide_latin_period_rule[];
extern ConvRule whiz_kana_latin_period_rule[];

// comma rule
extern ConvRule whiz_romakana_ja_comma_rule[];
extern ConvRule whiz_romakana_wide_latin_comma_rule[];
extern ConvRule whiz_romakana_latin_comma_rule[];

extern ConvRule whiz_kana_ja_comma_rule[];
extern ConvRule whiz_kana_wide_latin_comma_rule[];
extern ConvRule whiz_kana_latin_comma_rule[];

// space rule
extern ConvRule whiz_wide_space_rule[];
extern ConvRule whiz_space_rule[];

// misc
extern HiraganaKatakanaRule whiz_hiragana_katakana_table[];
extern WideRule             whiz_wide_table[];

#endif /* __WHIZ_CONV_TABLE_H__ */
