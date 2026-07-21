struct CANNA_MODE {
	int mode;
	int len;
	char name[20];
};

CANNA_MODE cm[] = {
{CANNA_MODE_ZenHiraHenkanMode,	2,	"あ"},
{CANNA_MODE_ZenKataHenkanMode,	2,	"ア"},
{CANNA_MODE_HanKataHenkanMode,	1,	"ｱ"},
{CANNA_MODE_HanAlphaHenkanMode,	1,	"a"},
{CANNA_MODE_ZenAlphaHenkanMode,	2,	"ａ"},
{CANNA_MODE_KigoMode,		1,	"記号"},
{CANNA_MODE_HexMode,		4,	"16進数"},
{CANNA_MODE_BushuMode,		1,	"部首"},
{-1,				-1,	"不明"}
};
