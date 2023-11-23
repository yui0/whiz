#include <stdio.h>
#include "whiz_conv_table.h"

ConvRule whiz_romakana_typing_rule[] = {
#if 0
{"va",	"������",	""},
{"vi",	"������",	""},
{"vu",	"����",	""},
{"ve",	"������",	""},
{"vo",	"������",	""},
#else
{"va",	"����",	""},
{"vi",	"����",	""},
{"vu",	"��",	""},
{"ve",	"����",	""},
{"vo",	"����",	""},
#endif
{"vv",	"��",	"v"},
{"xx",	"��",	"x"},
{"kk",	"��",	"k"},
{"gg",	"��",	"g"},
{"ss",	"��",	"s"},
{"zz",	"��",	"z"},
{"jj",	"��",	"j"},
{"tt",	"��",	"t"},
{"dd",	"��",	"d"},
{"hh",	"��",	"h"},
{"ff",	"��",	"f"},
{"bb",	"��",	"b"},
{"pp",	"��",	"p"},
{"mm",	"��",	"m"},
{"yy",	"��",	"y"},
{"rr",	"��",	"r"},
{"ww",	"��",	"w"},
{"cc",	"��",	"c"},
{"kya",	"����",	""},
{"kyi",	"����",	""},
{"kyu",	"����",	""},
{"kye",	"����",	""},
{"kyo",	"����",	""},
{"gya",	"����",	""},
{"gyi",	"����",	""},
{"gyu",	"����",	""},
{"gye",	"����",	""},
{"gyo",	"����",	""},
{"sya",	"����",	""},
{"syi",	"����",	""},
{"syu",	"����",	""},
{"sye",	"����",	""},
{"syo",	"����",	""},
{"sha",	"����",	""},
{"shi",	"��",	""},
{"shu",	"����",	""},
{"she",	"����",	""},
{"sho",	"����",	""},
{"zya",	"����",	""},
{"zyi",	"����",	""},
{"zyu",	"����",	""},
{"zye",	"����",	""},
{"zyo",	"����",	""},
{"tya",	"����",	""},
{"tyi",	"����",	""},
{"tyu",	"����",	""},
{"tye",	"����",	""},
{"tyo",	"����",	""},
{"cha",	"����",	""},
{"chi",	"��",	""},
{"chu",	"����",	""},
{"che",	"����",	""},
{"cho",	"����",	""},
{"cya",	"����",	""},
{"cyi",	"����",	""},
{"cyu",	"����",	""},
{"cye",	"����",	""},
{"cyo",	"����",	""},
{"dya",	"�¤�",	""},
{"dyi",	"�¤�",	""},
{"dyu",	"�¤�",	""},
{"dye",	"�¤�",	""},
{"dyo",	"�¤�",	""},
{"tha",	"�Ƥ�",	""},
{"thi",	"�Ƥ�",	""},
{"thu",	"�Ƥ�",	""},
{"the",	"�Ƥ�",	""},
{"tho",	"�Ƥ�",	""},
{"dha",	"�Ǥ�",	""},
{"dhi",	"�Ǥ�",	""},
{"dhu",	"�Ǥ�",	""},
{"dhe",	"�Ǥ�",	""},
{"dho",	"�Ǥ�",	""},
{"nya",	"�ˤ�",	""},
{"nyi",	"�ˤ�",	""},
{"nyu",	"�ˤ�",	""},
{"nye",	"�ˤ�",	""},
{"nyo",	"�ˤ�",	""},
{"hya",	"�Ҥ�",	""},
{"hyi",	"�Ҥ�",	""},
{"hyu",	"�Ҥ�",	""},
{"hye",	"�Ҥ�",	""},
{"hyo",	"�Ҥ�",	""},
{"bya",	"�Ӥ�",	""},
{"byi",	"�Ӥ�",	""},
{"byu",	"�Ӥ�",	""},
{"bye",	"�Ӥ�",	""},
{"byo",	"�Ӥ�",	""},
{"pya",	"�Ԥ�",	""},
{"pyi",	"�Ԥ�",	""},
{"pyu",	"�Ԥ�",	""},
{"pye",	"�Ԥ�",	""},
{"pyo",	"�Ԥ�",	""},
{"fa",	"�դ�",	""},
{"fi",	"�դ�",	""},
{"fu",	"��",	""},
{"fe",	"�դ�",	""},
{"fo",	"�դ�",	""},
{"mya",	"�ߤ�",	""},
{"myi",	"�ߤ�",	""},
{"myu",	"�ߤ�",	""},
{"mye",	"�ߤ�",	""},
{"myo",	"�ߤ�",	""},
{"rya",	"���",	""},
{"ryi",	"�ꤣ",	""},
{"ryu",	"���",	""},
{"rye",	"�ꤧ",	""},
{"ryo",	"���",	""},
{"a",	"��",	""},
{"i",	"��",	""},
{"u",	"��",	""},
{"e",	"��",	""},
{"o",	"��",	""},
{"xa",	"��",	""},
{"xi",	"��",	""},
{"xu",	"��",	""},
{"xe",	"��",	""},
{"xo",	"��",	""},
{"la",	"��",	""},
{"li",	"��",	""},
{"lu",	"��",	""},
{"le",	"��",	""},
{"lo",	"��",	""},
{"ka",	"��",	""},
{"ki",	"��",	""},
{"ku",	"��",	""},
{"ke",	"��",	""},
{"ko",	"��",	""},
{"lka",	"��",	""},
{"xka",	"��",	""},
{"lke",	"��",	""},
{"xke",	"��",	""},
{"ga",	"��",	""},
{"gi",	"��",	""},
{"gu",	"��",	""},
{"ge",	"��",	""},
{"go",	"��",	""},
{"sa",	"��",	""},
{"si",	"��",	""},
{"su",	"��",	""},
{"se",	"��",	""},
{"so",	"��",	""},
{"za",	"��",	""},
{"zi",	"��",	""},
{"zu",	"��",	""},
{"ze",	"��",	""},
{"zo",	"��",	""},
{"ja",	"����",	""},
{"jya", "����",	""},
{"ji",	"��",	""},
{"jyi", "����",	""},
{"ju",	"����",	""},
{"jyu",	"����",	""},
{"je",	"����",	""},
{"jye",	"����",	""},
{"jo",	"����",	""},
{"jyo",	"����",	""},
{"ta",	"��",	""},
{"ti",	"��",	""},
{"tu",	"��",	""},
{"tsu",	"��",	""},
{"te",	"��",	""},
{"to",	"��",	""},
{"da",	"��",	""},
{"di",	"��",	""},
{"du",	"��",	""},
{"de",	"��",	""},
{"do",	"��",	""},
{"xtu",	"��",	""},
{"xtsu","��"	""},
{"ltu",	"��",	""},
{"ltsu","��"	""},
{"na",	"��",	""},
{"ni",	"��",	""},
{"nu",	"��",	""},
{"ne",	"��",	""},
{"no",	"��",	""},
{"ha",	"��",	""},
{"hi",	"��",	""},
{"hu",	"��",	""},
{"fu",	"��",	""},
{"he",	"��",	""},
{"ho",	"��",	""},
{"ba",	"��",	""},
{"bi",	"��",	""},
{"bu",	"��",	""},
{"be",	"��",	""},
{"bo",	"��",	""},
{"pa",	"��",	""},
{"pi",	"��",	""},
{"pu",	"��",	""},
{"pe",	"��",	""},
{"po",	"��",	""},
{"ma",	"��",	""},
{"mi",	"��",	""},
{"mu",	"��",	""},
{"me",	"��",	""},
{"mo",	"��",	""},
{"lya",	"��",	""},
{"xya",	"��",	""},
{"ya",	"��",	""},
{"lyu",	"��",	""},
{"xyu",	"��",	""},
{"yu",	"��",	""},
{"lyo",	"��",	""},
{"xyo",	"��",	""},
{"yo",	"��",	""},
{"ra",	"��",	""},
{"ri",	"��",	""},
{"ru",	"��",	""},
{"re",	"��",	""},
{"ro",	"��",	""},
{"xwa",	"��",	""},
{"wa",	"��",	""},
{"wi",	"����",	""},
{"we",	"����",	""},
{"wo",	"��",	""},
{"wha",	"����",	""},
{"whi",	"����",	""},
{"whe",	"����",	""},
{"who",	"����",	""},
{"wyi",	"��",	""},
{"wye",	"��",	""},
{"n'",	"��",	""},
/*{"n",	"��",	""},*/
{"nn",	"��",	""},
{"n",	"��",	""},
{"-",	"��",	""},
/* �������� */
{".",	"��",	""},
{",",	"��",	""},
{"[",	"��",	""},
{"]",	"��",	""},
{"/",	"��",	""},
#if 1 /* emulate dead key */
{"\\.",	"��",	""},
{";r",	"��",	""},
{";l",	"��",	""},
{";u",	"��",	""},
{";d",	"��",	""},
{";p",	"��",	""},
#endif
{NULL,	NULL,	NULL}
};

ConvRule whiz_romakana_symbol_rule[] = {
{",",	",",	""},
{".",	".",	""},
{"!",	"!",	""},
{"\"",	"\"",	""},
{"#",	"#",	""},
{"$",	"$",	""},
{"%",	"%",	""},
{"&",	"&",	""},
{"'",	"'",	""},
{"(",	"(",	""},
{")",	")",	""},
{"~",	"~",	""},
{"=",	"=",	""},
{"^",	"^",	""},
{"\\",	"\\",	""},
{"|",	"|",	""},
{"`",	"`",	""},
{"@",	"@",	""},
{"{",	"{",	""},
{"[",	"[",	""},
{"+",	"+",	""},
{";",	";",	""},
{"*",	"*",	""},
{":",	":",	""},
{"}",	"}",	""},
{"]",	"]",	""},
{"<",	"<",	""},
{">",	">",	""},
{"?",	"?",	""},
{"/",	"/",	""},
{"_",	"_",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_romakana_wide_symbol_rule[] = {
{",",	"��",	""},
{".",	"��",	""},
{"!",	"��",	""},
{"\"",	"��",	""},
{"#",	"��",	""},
{"$",	"��",	""},
{"%",	"��",	""},
{"&",	"��",	""},
{"'",	"��",	""},
{"(",	"��",	""},
{")",	"��",	""},
{"~",	"��",	""},
{"=",	"��",	""},
{"^",	"��",	""},
{"\\",	"��",	""},
{"|",	"��",	""},
{"`",	"��",	""},
{"@",	"��",	""},
{"{",	"��",	""},
{"[",	"��",	""},
{"+",	"��",	""},
{";",	"��",	""},
{"*",	"��",	""},
{":",	"��",	""},
{"}",	"��",	""},
{"]",	"��",	""},
{"<",	"��",	""},
{">",	"��",	""},
{"?",	"��",	""},
{"/",	"��",	""},
{"_",	"��",	""},
{NULL,	NULL,	NULL}
};

ConvRule whiz_romakana_number_rule[] = {
{"1", "1", ""},
{"2", "2", ""},
{"3", "3", ""},
{"4", "4", ""},
{"5", "5", ""},
{"6", "6", ""},
{"7", "7", ""},
{"8", "8", ""},
{"9", "9", ""},
{"0", "0", ""},
{NULL,	NULL,	NULL}
};


ConvRule whiz_romakana_wide_number_rule[] = {
{"1", "��", ""},
{"2", "��", ""},
{"3", "��", ""},
{"4", "��", ""},
{"5", "��", ""},
{"6", "��", ""},
{"7", "��", ""},
{"8", "��", ""},
{"9", "��", ""},
{"0", "��", ""},
{NULL,	NULL,	NULL}
};

ConvRule whiz_kana_typing_rule[] = {
{"t@",	"��",	""},
{"g@",	"��",	""},
{"h@",	"��",	""},
{":@",	"��",	""},
{"b@",	"��",	""},
{"x@",	"��",	""},
{"d@",	"��",	""},
{"r@",	"��",	""},
{"p@",	"��",	""},
{"c@",	"��",	""},
{"q@",	"��",	""},
{"a@",	"��",	""},
{"z@",	"��",	""},
{"w@",	"��",	""},
{"s@",	"��",	""},
{"f@",	"��",	""},
{"v@",	"��",	""},
{"2@",	"��",	""},
{"^@",	"��",	""},
{"-@",	"��",	""},
{"f[",	"��",	""},
{"v[",	"��",	""},
{"2[",	"��",	""},
{"^[",	"��",	""},
{"-[",	"��",	""},
{"#",	"��",	""},
{"E",	"��",	""},
{"$",	"��",	""},
{"%",	"��",	""},
{"&",	"��",	""},
{"'",	"��",	""},
{"(",	"��",	""},
{")",	"��",	""},
{"~",	"��",	""},
{"Z",	"��",	""},
{"y",	"��",	""},
{"3",	"��",	""},
{"e",	"��",	""},
{"4",	"��",	""},
{"5",	"��",	""},
{"6",	"��",	""},
{"t",	"",	"��"},
{"g",	"",	"��"},
{"h",	"",	"��"},
{":",	"",	"��"},
{"b",	"",	"��"},
{"x",	"",	"��"},
{"d",	"",	"��"},
{"r",	"",	"��"},
{"p",	"",	"��"},
{"c",	"",	"��"},
{"q",	"",	"��"},
{"a",	"",	"��"},
{"z",	"",	"��"},
{"w",	"",	"��"},
{"s",	"",	"��"},
{"u",	"��",	""},
{"i",	"��",	""},
{"1",	"��",	""},
{",",	"��",	""},
{"k",	"��",	""},
{"f",	"",	"��"},
{"v",	"",	"��"},
{"2",	"",	"��"},
{"^",	"",	"��"},
{"-",	"",	"��"},
{"j",	"��",	""},
{"n",	"��",	""},
{"]",	"��",	""},
{"/",	"��",	""},
{"m",	"��",	""},
{"7",	"��",	""},
{"8",	"��",	""},
{"9",	"��",	""},
{"o",	"��",	""},
{"l",	"��",	""},
{".",	"��",	""},
{";",	"��",	""},
{"0",	"��",	""},
{"|",	"��",	""},
{"\\",	"��",	""},
#if 0
/* �嵭�Τ褦���ѹ� */
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��[",	"��",	""},
{"��[",	"��",	""},
{"��[",	"��",	""},
{"��[",	"��",	""},
{"��[",	"��",	""},
#endif
{">",	"��",	""},
{"<",	"��",	""},
{"?",	"��",	""},
{"@",	"��",	""},
{"[",	"��",	""},
{"{",	"��",	""},
{"}",	"��",	""},
{NULL,	NULL,	NULL},
};
#if 0
ConvRule whiz_kana_typing_rule[] = {
{"#",	"��",	""},
{"E",	"��",	""},
{"$",	"��",	""},
{"%",	"��",	""},
{"&",	"��",	""},
{"'",	"��",	""},
{"(",	"��",	""},
{")",	"��",	""},
{"~",	"��",	""},
{"Z",	"��",	""},
{"y",	"��",	""},
{"3",	"��",	""},
{"e",	"��",	""},
{"4",	"��",	""},
{"5",	"��",	""},
{"6",	"��",	""},
{"t",	"",	"��"},
{"g",	"",	"��"},
{"h",	"",	"��"},
{":",	"",	"��"},
{"b",	"",	"��"},
{"x",	"",	"��"},
{"d",	"",	"��"},
{"r",	"",	"��"},
{"p",	"",	"��"},
{"c",	"",	"��"},
{"q",	"",	"��"},
{"a",	"",	"��"},
{"z",	"",	"��"},
{"w",	"",	"��"},
{"s",	"",	"��"},
{"u",	"��",	""},
{"i",	"��",	""},
{"1",	"��",	""},
{",",	"��",	""},
{"k",	"��",	""},
{"f",	"",	"��"},
{"v",	"",	"��"},
{"2",	"",	"��"},
{"^",	"",	"��"},
{"-",	"",	"��"},
{"j",	"��",	""},
{"n",	"��",	""},
{"]",	"��",	""},
{"/",	"��",	""},
{"m",	"��",	""},
{"7",	"��",	""},
{"8",	"��",	""},
{"9",	"��",	""},
{"o",	"��",	""},
{"l",	"��",	""},
{".",	"��",	""},
{";",	"��",	""},
{"0",	"��",	""},
{"|",	"��",	""},
{"\\",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��@",	"��",	""},
{"��[",	"��",	""},
{"��[",	"��",	""},
{"��[",	"��",	""},
{"��[",	"��",	""},
{"��[",	"��",	""},
{">",	"��",	""},
{"<",	"��",	""},
{"?",	"��",	""},
{"@",	"��",	""},
{"[",	"��",	""},
{"{",	"��",	""},
{"}",	"��",	""},
{NULL,	NULL,	NULL},
};
#endif

ConvRule whiz_romakana_ja_period_rule[] = {
{".",	"��",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_romakana_ja_comma_rule[] = {
{",",	"��",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_romakana_wide_latin_period_rule[] = {
{".",	"��",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_romakana_wide_latin_comma_rule[] = {
{",",	"��",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_romakana_latin_period_rule[] = {
{".",	".",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_romakana_latin_comma_rule[] = {
{",",	",",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_kana_ja_period_rule[] = {
{">",	"��",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_kana_ja_comma_rule[] = {
{"<",	"��",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_kana_wide_latin_period_rule[] = {
{">",	"��",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_kana_wide_latin_comma_rule[] = {
{"<",	"��",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_kana_latin_period_rule[] = {
{">",	".",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_kana_latin_comma_rule[] = {
{"<",	",",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_wide_space_rule[] = {
{" ",	"��",	""},
{NULL,	NULL,	NULL},
};

ConvRule whiz_space_rule[] = {
{" ",	" ",	""},
{NULL,	NULL,	NULL},
};

HiraganaKatakanaRule whiz_hiragana_katakana_table[] = {
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "����"},
{"��", "��", "����"},
{"��", "��", "����"},
{"��", "��", "����"},
{"��", "��", "����"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "����"},
{"��", "��", "����"},
{"��", "��", "����"},
{"��", "��", "����"},
{"��", "��", "����"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "����"},
{"��", "��", "����"},
{"��", "��", "��"},
{"��", "��", "�Î�"},
{"��", "��", "�Ď�"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "�ʎ�"},
{"��", "��", "�ˎ�"},
{"��", "��", "�̎�"},
{"��", "��", "�͎�"},
{"��", "��", "�Ύ�"},
{"��", "��", "�ʎ�"},
{"��", "��", "�ˎ�"},
{"��", "��", "�̎�"},
{"��", "��", "�͎�"},
{"��", "��", "�Ύ�"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},

{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},

{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},

{"��", "��", "��"},

{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},

{"��", "��", "��"},
{"��", "��", "��"},

{"��", "��", "��"},
{"��", "��", "��"},
{"��", "��", "��"},

#if 1
//{"����" "��" "����"},
{"��" "��" "����"},
#endif

#if 1
{"��",	"��",	"��"},
{"��",	"��",	"��"},
{"��",	"��",	"��"},
{"��",	"��",	"!"},
{"��",	"��",	"\""},
{"��",	"��",	"#"},
{"��",	"��",	"$"},
{"��",	"��",	"%"},
{"��",	"��",	"&"},
{"��",	"��",	"'"},
{"��",	"��",	"("},
{"��",	"��",	")"},
{"��",	"��",	"~"},
{"��",	"��",	"="},
{"��",	"��",	"^"},
{"��",	"��",	"\\"},
{"��",	"��",	"|"},
{"��",	"��",	"`"},
{"��",	"��",	"@"},
{"��",	"��",	"{"},
{"��",	"��",	"��"},
{"��",	"��",	"+"},
{"��",	"��",	";"},
{"��",	"��",	"*"},
{"��",	"��",	":"},
{"��",	"��",	"}"},
{"��",	"��",	"��"},
{"��",	"��",	"<"},
{"��",	"��",	">"},
{"��",	"��",	"?"},
{"��",	"��",	"/"},
{"��",	"��",	"_"},
#endif
{NULL,	NULL,	NULL},
};

/* from uim */
WideRule whiz_wide_table[] = {
{"a", "��"},
{"b", "��"},
{"c", "��"},
{"d", "��"},
{"e", "��"},
{"f", "��"},
{"g", "��"},
{"h", "��"},
{"i", "��"},
{"j", "��"},
{"k", "��"},
{"l", "��"},
{"m", "��"},
{"n", "��"},
{"o", "��"},
{"p", "��"},
{"q", "��"},
{"r", "��"},
{"s", "��"},
{"t", "��"},
{"u", "��"},
{"v", "��"},
{"w", "��"},
{"x", "��"},
{"y", "��"},
{"z", "��"},
{"A", "��"},
{"B", "��"},
{"C", "��"},
{"D", "��"},
{"E", "��"},
{"F", "��"},
{"G", "��"},
{"H", "��"},
{"I", "��"},
{"J", "��"},
{"K", "��"},
{"L", "��"},
{"M", "��"},
{"N", "��"},
{"O", "��"},
{"P", "��"},
{"Q", "��"},
{"R", "��"},
{"S", "��"},
{"T", "��"},
{"U", "��"},
{"V", "��"},
{"W", "��"},
{"X", "��"},
{"Y", "��"},
{"Z", "��"},
{"1", "��"},
{"2", "��"},
{"3", "��"},
{"4", "��"},
{"5", "��"},
{"6", "��"},
{"7", "��"},
{"8", "��"},
{"9", "��"},
{"0", "��"},
{"-", "��"},
{",", "��"},
{".", "��"},
{"!", "��"},
{"\"", "��"},
{"#", "��"},
{"$", "��"},
{"%", "��"},
{"&", "��"},
{"'", "��"},
{"(", "��"},
{")", "��"},
{"~", "��"},
{"=", "��"},
{"^", "��"},
{"\\", "��"},
{"|", "��"},
{"`", "��"},
{"@", "��"},
{"{", "��"},
{"[", "��"},
{"+", "��"},
{";", "��"},
{":", "��"},
{"}", "��"},
{"]", "��"},
{"<", "��"},
{">", "��"},
{"?", "��"},
{"/", "��"},
{"_",  "��"},
{NULL, NULL},
};