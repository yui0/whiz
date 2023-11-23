//---------------------------------------------------------
//	Whiz Server (Japanese Input Method Engine)
//
//		©2003-2006,2015 Yuichiro Nakada
//---------------------------------------------------------

/* どのような情報があるかを示すフラグ */
#define KanjiModeInfo   	0x1	// 変換モード変更
#define KanjiGLineInfo  	0x2	// 候補を表示
#define KanjiYomiInfo		0x4
#define KanjiThroughInfo	0x8	// そのまま表示
#define KanjiEmptyInfo		0x10

#define KanjiExtendInfo		0x20
#define KanjiKigoInfo		0x40
#define KanjiRussianInfo	0x80
#define KanjiGreekInfo		0x100
#define KanjiLineInfo		0x200

#define KanjiAttributeInfo	0x400
#define KanjiSpecialFuncInfo	0x800

/* KanjiControl 関係 */
#define KC_INITIALIZE		0
#define KC_FINALIZE		1
#define KC_CHANGEMODE		2
#define KC_SETWIDTH		3
#define KC_SETUNDEFKEYFUNCTION	4
#define KC_SETBUNSETSUKUGIRI    5
#define KC_SETMODEINFOSTYLE	6
#define KC_SETHEXINPUTSTYLE	7
#define KC_INHIBITHANKAKUKANA	8
#define KC_DEFINEKANJI		9
#define KC_KAKUTEI		10
#define KC_KILL			11
#define KC_MODEKEYS		12
#define KC_QUERYMODE		13
#define KC_QUERYCONNECTION	14
#define KC_SETSERVERNAME        15
#define KC_PARSE		16
#define KC_YOMIINFO		17
#define KC_STOREYOMI		18
#define KC_SETINITFILENAME	19
#define KC_DO			20
#define KC_GETCONTEXT		21
#define KC_CLOSEUICONTEXT	22
#define KC_INHIBITCHANGEMODE	23
#define KC_LETTERRESTRICTION	24
#define KC_QUERYMAXMODESTR	25
#define KC_SETLISTCALLBACK	26
#define KC_SETVERBOSE		27
#define KC_LISPINTERACTION	28
#define KC_DISCONNECTSERVER	29
#define KC_SETAPPNAME	        30
#define KC_DEBUGMODE	        31
#define KC_DEBUGYOMI	        32
#define KC_KEYCONVCALLBACK	33
#define KC_QUERYPHONO		34
#define KC_CHANGESERVER		35
#define KC_SETUSERINFO          36
#define KC_QUERYCUSTOM          37
#define KC_CLOSEALLCONTEXT      38
#define KC_ATTRIBUTEINFO	39
#define KC_CHANGERULE		40
#define KC_EX_RECONVERT		41

typedef struct {
	unsigned char *echoStr;	/* local echo string */
	int length;		/* length of echo string */
	int revPos;		/* reverse position  */
	int revLen;		/* reverse length    */
	unsigned long info;	/* その他の情報 */
	unsigned char *mode;	/* モード情報 */
	struct {
		unsigned char *line;	// 候補リスト
		int length;		// 候補リストの長さ
		int revPos;
		int revLen;
	} gline;		/* 一覧表示のための情報 */
} jrKanjiStatus;

typedef struct {
	int val;
	unsigned char *buffer;
	int bytes_buffer;
	jrKanjiStatus *ks;
} jrKanjiStatusWithValue;

#pragma once
#ifdef __cplusplus
extern "C"
{
#endif
int jrKanjiString(const int, int, char *, const int, jrKanjiStatus *);
int jrKanjiControl(const int, const int, char *);
#ifdef __cplusplus
}
#endif


/* real modes */
/* 実モード(real mode)はキーマップの実体を持っているモード */

#define CANNA_MODE_AlphaMode		0	/* アルファベットモード */
#define CANNA_MODE_EmptyMode		1	/* 読み入力がない状態 */
#define CANNA_MODE_KigoMode		2	/* 記号一覧表示状態 */
#define CANNA_MODE_YomiMode		3	/* 読み入力している状態 */
#define CANNA_MODE_JishuMode		4	/* 文字種変換している状態 */
#define CANNA_MODE_TankouhoMode		5	/* 単一候補表示状態 */
#define CANNA_MODE_IchiranMode		6	/* 候補一覧表示状態 */
#define CANNA_MODE_YesNoMode		7	/* 単語登録の例文表示状態 */
#define CANNA_MODE_OnOffMode		8	/* 単語登録の例文表示状態 */
#define CANNA_MODE_AdjustBunsetsuMode   9	/* 文節伸縮モード */
#define CANNA_MODE_ChikujiYomiMode	10	/* 逐次変換モードの読み部分 */
#define CANNA_MODE_ChikujiTanMode	11	/* 逐次変換モードの候補部分 */

#define CANNA_MODE_MAX_REAL_MODE	(CANNA_MODE_ChikujiTanMode + 1)

/* imaginary modes */
/* 虚モード(imaginary mode)はキーマップの実体を持っていないモード */

#define CANNA_MODE_HenkanMode		CANNA_MODE_EmptyMode
#define CANNA_MODE_HenkanNyuryokuMode	12

#define CANNA_MODE_ZenHiraHenkanMode	13
#define CANNA_MODE_HanHiraHenkanMode	14
#define CANNA_MODE_ZenKataHenkanMode	15
#define CANNA_MODE_HanKataHenkanMode	16
#define CANNA_MODE_ZenAlphaHenkanMode	17
#define CANNA_MODE_HanAlphaHenkanMode	18

#define CANNA_MODE_ZenHiraKakuteiMode	19
#define CANNA_MODE_HanHiraKakuteiMode	20
#define CANNA_MODE_ZenKataKakuteiMode	21
#define CANNA_MODE_HanKataKakuteiMode	22
#define CANNA_MODE_ZenAlphaKakuteiMode	23
#define CANNA_MODE_HanAlphaKakuteiMode	24

#define CANNA_MODE_HexMode		25	/* １６進コード入力モード */
#define CANNA_MODE_BushuMode		26	/* 部首の読みの入力状態 */
#define CANNA_MODE_ExtendMode		27	/* 拡張機能選択 */
#define CANNA_MODE_RussianMode		28	/* ロシア文字選択 */
#define CANNA_MODE_GreekMode		29	/* ギリシア文字選択 */
#define CANNA_MODE_LineMode		30	/* 罫線選択 */
#define CANNA_MODE_ChangingServerMode	31	/* サーバ変更 */
#define CANNA_MODE_HenkanMethodMode	32	/* 変換方式選択 */
#define CANNA_MODE_DeleteDicMode	33	/* 単語削除 */
#define CANNA_MODE_TourokuMode		34	/* 単語登録モード */
#define CANNA_MODE_TourokuEmptyMode	CANNA_MODE_TourokuMode
#define CANNA_MODE_TourokuHinshiMode	35	/* 単語登録の品詞選択状態 */
#define CANNA_MODE_TourokuDicMode	36	/* 単語登録の辞書選択状態 */
#define CANNA_MODE_QuotedInsertMode	37	/* 引用入力モード */
#define CANNA_MODE_BubunMuhenkanMode	38	/* 部分無変換状態 */
#define CANNA_MODE_MountDicMode   	39	/* 辞書のmount,unmount状態 */

#define CANNA_MODE_MAX_IMAGINARY_MODE	(CANNA_MODE_MountDicMode + 1)


#include "keydef.h"
