struct CANNA_MODE {
	int mode;
	int len;
	char name[20];
};

CANNA_MODE cm[] = {
{CANNA_MODE_ZenHiraHenkanMode,	2,	"��"},
{CANNA_MODE_ZenKataHenkanMode,	2,	"��"},
{CANNA_MODE_HanKataHenkanMode,	1,	"��"},
{CANNA_MODE_HanAlphaHenkanMode,	1,	"a"},
{CANNA_MODE_ZenAlphaHenkanMode,	2,	"��"},
{CANNA_MODE_KigoMode,		1,	"����"},
{CANNA_MODE_HexMode,		4,	"16�ʿ�"},
{CANNA_MODE_BushuMode,		1,	"����"},
{-1,				-1,	"����"}
};
