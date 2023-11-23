//---------------------------------------------------------
//	Whiz Server (Japanese Input Method Engine)
//
//		©2003-2006,2015 Yuichiro Nakada
//---------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include "jrkanji.h"

#include "canna_mode.h"
#include "whiz_conv_table.h"

#include "engine/whiz.h"
#include "debug/debug.h"

#define uint8_t		unsigned char

#define MAX_CAND	10


// ローマ字をかな文字に変換
int roma2kana(char *p, char *s)
{
	int m, n;

	// 入力がない場合
	if (!*s) return 0;

	*p=0;	// 初期化
	do {
		n=0;
		do {
			m=strlen(whiz_romakana_typing_rule[n].string);
			if (!strncmp(whiz_romakana_typing_rule[n].string, s, m)) {
				strcat(p, whiz_romakana_typing_rule[n].result);
				// 「itta」→「いった」など...
				if (strlen(whiz_romakana_typing_rule[n].cont)>0) s--;
				s+=m;
				break;
			}
			n++;
		} while (whiz_romakana_typing_rule[n].string);
		if (!whiz_romakana_typing_rule[n].string) {
			// 変換できない
			strncat(p, s, 1);
			s++;
			m=1;
		}
	} while (*s);

	return m;
}

// かな入力をかな文字に変換
void jkey2kana(char *p, char *s)
{
	int m, n;

	// 入力がない場合
	if (!*s) return;

	*p=0;	// 初期化
	do {
		n=0;
		do {
			m=strlen(whiz_kana_typing_rule[n].string);
			if (!strncmp(whiz_kana_typing_rule[n].string, s, m)) {
				if (strlen(whiz_kana_typing_rule[n].result)) {
					// 濁音なし
					strcat(p, whiz_kana_typing_rule[n].result);
				} else {
					// 濁音あり
					strcat(p, whiz_kana_typing_rule[n].cont);
				}
				s+=m;
				break;
			}
			n++;
		} while (whiz_kana_typing_rule[n].string);
		if (!whiz_kana_typing_rule[n].string) {
			// 変換できない
			strncat(p, s, 1);
			s++;
		}
	} while (*s);

	return;
}

// かな文字をカナ文字に変換
void kana2kata(char *p, char *s)
{
	int m, n;

	// 入力がない場合
	if (!*s) return;

	*p=0;
	do {
		n=0;
		do {
			m=strlen(whiz_hiragana_katakana_table[n].hiragana);
			if (!strncmp(whiz_hiragana_katakana_table[n].hiragana, s, m)) {
				strcat(p, whiz_hiragana_katakana_table[n].katakana);
				s+=m;
				break;
			}
			n++;
		} while (whiz_hiragana_katakana_table[n].hiragana);
		if (!whiz_hiragana_katakana_table[n].hiragana) {
			// 変換できない
			strncat(p, s, 1);
			s++;
		}
	} while (*s);

	return;
}

// かな文字をｶﾅ文字に変換
void kana2half_kata(char *p, char *s)
{
	int m, n;

	// 入力がない場合
	if (!*s) return;

	*p=0;
	do {
		n=0;
		do {
			m=strlen(whiz_hiragana_katakana_table[n].hiragana);
			if (!strncmp(whiz_hiragana_katakana_table[n].hiragana, s, m)) {
				strcat(p, whiz_hiragana_katakana_table[n].half_katakana);
				s+=m;
				break;
			}
			n++;
		} while (whiz_hiragana_katakana_table[n].hiragana);
		if (!whiz_hiragana_katakana_table[n].hiragana) {
			// 変換できない
			strncat(p, s, 1);
			s++;
		}
	} while (*s);

	return;
}

// 全角アルファベットに変換
void alpha2zen_a(char *p, char *s)
{
	int m, n;

	// 入力がない場合
	if (!*s) return;

	*p=0;
	do {
		n=0;
		do {
			m=strlen(whiz_wide_table[n].code);
			if (!strncmp(whiz_wide_table[n].code, s, m)) {
				strcat(p, whiz_wide_table[n].wide);
				s+=m;
				break;
			}
			n++;
		} while (whiz_wide_table[n].code);
		if (!whiz_wide_table[n].code) {
			// 変換できない
			strncat(p, s, 1);
			s++;
		}
	} while (*s);

	return;
}

int get_canna_mode(int mode)
{
	int i;

	if (mode == CANNA_MODE_HenkanMode) return 0;

	i=0;
	while (cm[i].mode>=0) {
		if (cm[i].mode == mode) break;
		i++;
	}

	return i;
}

class WHIZ_INPUT {
public:
	int mode;		// 変換モード
	int rule;		// 変換方式
	int kanji;		// 漢字変換中？
	int seg, cand;		// 文節と候補

	int revPos, revLen;	// 反転開始位置&長さ

	char s[LINEMAX];	// 保存文字列(ローマ字)
	char js[LINEMAX];	// 保存文字列(かな/漢字)

	char kan[MAXSEGMENT][WORDMAX];

	WHIZ_INPUT() : mode(0), rule(0), kanji(0)
	{
		s[0]=js[0]=0;
	}
};

WHIZ whiz;
WHIZ_INPUT wi;

inline void kana(char *p, char *s)
{
	if (!wi.rule) roma2kana(p, s);
	else jkey2kana(p, s);
}

extern "C" {

// EUC-JP文字のバイト数を取得
int eucjp_clen(uint8_t c)
{
	if (c == 0x8f) return 3;
	if (((c >= 0xa1) && (c <= 0xfe)) || c == 0x8e) return 2;
	return 1;
}

// 漢字に変換する
void kconvert(jrKanjiStatus *ks)
{
	static char ss[LINEMAX];

	whiz.analysis(wi.js);	// 解析
	whiz.convert(ss);	// 結果を出力
	//ks->revPos=0;
	ks->revLen=strlen(ss);

	wi.kanji=1;
	wi.seg=0;
	wi.cand=1;
	wi.revPos=0;
	wi.revLen=ks->revLen;

	// 配列にコピー
	char *s=ss;
	int n=0;
	do {
		char *p=wi.kan[n++];
		while (*s) *p++=*s++;
		*p=0;
		s++;
	} while (*s);

	// 表示用
	wi.js[0]=0;
	for (n=0; n<whiz.seg; n++) strcat(wi.js, wi.kan[n]);

	ks->echoStr=(uint8_t*)wi.js;
	ks->length=strlen(wi.js);
}

int jrKanjiControl(const int context, const int request, char *arg)
{
	switch (request) {
	case KC_INITIALIZE:
		// 初期化(成功0/失敗-1)
		whiz.initialize();
	case KC_FINALIZE:
		// 終了(成功0/失敗-1)
		if (arg) *arg=0;	// 警告
		break;
	case KC_CHANGEMODE:
		// 入力モード変更
		wi.mode=get_canna_mode(((jrKanjiStatusWithValue*)arg)->val);
		((jrKanjiStatusWithValue*)arg)->val=cm[wi.mode].len;
		break;
	case KC_QUERYMODE:
		// 変換モード問い合わせ
		strcpy(arg, cm[wi.mode].name);
		break;
	case KC_QUERYMAXMODESTR:
		// 変換モード文字列の最大文字数
		return 40;
	case KC_SETINITFILENAME:
		debug(dmsg(10, "Init file <%s>\n", arg);)
		break;
	case KC_SETSERVERNAME:
		debug(dmsg(10, "Server <%s>\n", arg);)
		break;
	case KC_SETAPPNAME:
		debug(dmsg(10, "App <%s>\n", arg);)
		break;
	//case KC_CLOSEUICONTEXT:
	case KC_CHANGERULE:
		// 入力方式変更
		wi.rule=((jrKanjiStatusWithValue*)arg)->val;
		break;
	case KC_EX_RECONVERT:
		// 再変換
		jrKanjiStatus *ks = ((jrKanjiStatus*)arg);
//		printf("再変換:%s\n", ks->echoStr);
		strcpy(wi.s, (char*)ks->echoStr);
		strcpy(wi.js, (char*)ks->echoStr);
//		ks->echoStr=(uint8_t*)wi.js;
//		ks->length=strlen(wi.js);
//		wi.revPos = ks->revPos = ks->length;
		//ks->revPos=0;
//		ks->revLen=0;

		// 変換
		static char ss[LINEMAX];
		whiz.analysis(wi.js);	// 解析
		whiz.convert(ss);	// 結果を出力
		ks->revLen=strlen(ss);
		wi.js[0]=0;
		for (int n=0; n<whiz.seg; n++) {
			char s[LINEMAX];
			int c;
			whiz.get_candidacy(n, s, c);
			char *p = s; // 最後のカナだけコピー
			for (int i=0; i<c; i++) {
				while (*p) p++;
				p++;
			}
			strcat(wi.js, p);
//			printf("%d:%s\n", n, p);
		}
		kconvert(ks);
		wi.revPos = ks->revPos = ks->length;
	}
	return 0;
}

int jrKanjiString(const int context, int ch, char *buff, const int nbuff, jrKanjiStatus *ks)
{
	int i, j, n;

	// 初期化
	ks->info = 0;
	ks->echoStr=NULL;
	ks->length=0;
	ks->revPos=0;		// 反転開始位置
	ks->revLen=0;		// 反転させる長さ
	ks->gline.line=(uint8_t*)"";
	ks->gline.length=0;
	ks->gline.revPos=0;
	ks->gline.revLen=0;

	if (wi.kanji>1) ks->info|=KanjiGLineInfo;

	switch (wi.mode) {
	case 3:
		// アルファベット
		*buff=ch;
		buff[1]=0;
		ks->info |= KanjiThroughInfo;
		//ks->length=0;
		return 1;
	case 4:
		// 全角アルファベット
		*buff=ch;
		buff[1]=0;

		switch (ch) {
		case CANNA_KEY_F6:
			// ひらがな
			wi.mode=get_canna_mode(CANNA_MODE_ZenHiraHenkanMode);
			ks->info|=KanjiModeInfo;
			return 0;
		}

		if (ch >= 0x21 && ch <=0x7e) {
			// 半角から全角に変換
			n=0;
			do {
				if (whiz_wide_table[n].code[0] == (char)ch) {
					strcpy(buff, whiz_wide_table[n].wide);
					break;
				}
				n++;
			} while (whiz_wide_table[n].code);
		} else {
			// 制御コード
			ks->info |= KanjiThroughInfo;
		}
		//ks->length=0;
		return strlen(buff);
	}

	switch (ch) {
	case 0x09: // Tab
	case 0x0d: // Enter
		// 改行(確定)
		n=strlen(wi.js);
		if (n<1) {
			// 変換中じゃない
			*buff=ch;
			buff[1]=0;
			ks->info |= KanjiThroughInfo;
			return 1;
		}

		// 文字列を確定しコピー
		strcpy(buff, wi.js);

		ks->length=0;
		wi.kanji=0;
		wi.s[0]=0;
		wi.js[0]=0;

		return n;

	case 0x08:
		// バックスペース
		if (wi.kanji>0) {
			// 変換中
			wi.kanji=0;
			ch=0;
			break;
		}
		n=strlen(wi.s);
		if (n<1) {
			// 変換中じゃない
			*buff=ch;
			buff[1]=0;
			ks->info |= KanjiThroughInfo;
			return 1;
		}

		n-=roma2kana(wi.js, wi.s); // 最後一文字の文字数を引く
		wi.s[n]=0;

//		wi.s[n-1]=0;
		ch=0;
		//ks->echoStr=(uint8_t*)wi.s;
		//ks->length=strlen(wi.s);
		break;

	case 0x20:
	case CANNA_KEY_Down:
	case CANNA_KEY_Up:
	case CANNA_KEY_Rolldown:
	case CANNA_KEY_Rollup:
		// スペース(変換開始)
		static char ss[LINEMAX], cs[LINEMAX];
		char *p, *s;
		if (wi.kanji>0) {
			// 候補取得
			wi.kanji=2;
			ks->info|=KanjiGLineInfo;
			whiz.get_candidacy(wi.seg, ss, n);
			n++;	// 候補数

			// 次の候補へ
			if (ch==CANNA_KEY_Rollup) {
				wi.cand-=MAX_CAND;
				if (wi.cand<=0) wi.cand=n;
			} else if (ch==CANNA_KEY_Rolldown) {
				wi.cand+=MAX_CAND;
				if (wi.cand>n) wi.cand=1;
			} else if (ch==CANNA_KEY_Up) {
				wi.cand--;
				if (wi.cand<=0) wi.cand=n;
			} else {
				wi.cand++;
				if (wi.cand>n) wi.cand=1;
			}

			// 初めの部分を飛ばす
			int m=1;
			s=ss;
			if (wi.cand>MAX_CAND) {
				for (i=(wi.cand-1)/MAX_CAND; i>0; i--) {
					for (j=0; j<MAX_CAND; j++) {
						while (*s) s++;
						s++;
						m++;
					}
				}
			}

			// \0を抜かす
			p=cs;
			if (m-1+MAX_CAND > n) i=n%MAX_CAND;
			else i=MAX_CAND;
			for (; i>0; i--) {
			//do {
				sprintf(p, "%d.", m); // 候補番号
				while (*p) p++;
				if (m==wi.cand) ks->gline.revPos=p-cs;
				while (*s) *p++=*s++;
				if (m++==wi.cand) ks->gline.revLen=(int)(p-cs)-ks->gline.revPos;
				s++;
				*p++=0x20; // スペースで区切る
			//} while (*s);
			}
			sprintf(p, ">>%d/%d", wi.cand, n); // 候補数

			ks->gline.line=(uint8_t*)cs;
			ks->gline.length=strlen(cs);

			// 候補入れ替え
			strncpy(wi.kan[wi.seg], cs+ks->gline.revPos, ks->gline.revLen);
			wi.kan[wi.seg][ks->gline.revLen]=0;
			wi.js[0]=0;
			for (i=0; i<whiz.seg; i++) strcat(wi.js, wi.kan[i]);
			/*ss[0]=0;
			strncpy(ss, wi.js, wi.revPos);
			strncat(ss, cs+ks->gline.revPos, ks->gline.revLen);
			strcat(ss, wi.js+wi.revPos+wi.revLen);
			wi.revLen=ks->gline.revLen;
			strcpy(wi.js, ss);*/

			ks->echoStr=(uint8_t*)wi.js;
			ks->length=strlen(wi.js);
			return 0;
		}
		if (ch==CANNA_KEY_Up || ch==CANNA_KEY_Down || ch==CANNA_KEY_Rolldown || ch==CANNA_KEY_Rollup) {
			// キー操作
			*buff=ch;
			buff[1]=0;
			ks->info |= KanjiThroughInfo;
			return 1;
		}
		if (strlen(wi.js)<=0) {
			// ただのスペースを
			*buff=ch;
			buff[1]=0;
			ks->info |= KanjiThroughInfo;
			return 1;
		}
#if 0
		wi.kanji=1;

		// 変換
		whiz.analysis(wi.js);	// 解析
		whiz.convert(ss);	// 結果を出力
		//ks->revPos=0;
		ks->revLen=strlen(ss);
		wi.seg=0;
		wi.cand=1;
		wi.revPos=0;
		wi.revLen=ks->revLen;

		// 配列にコピー
		n=0;
		s=ss;
		do {
			p=wi.kan[n++];
			while (*s) *p++=*s++;
			*p=0;
			s++;
		} while (*s);
		// 表示用
		wi.js[0]=0;
		for (n=0; n<whiz.seg; n++) strcat(wi.js, wi.kan[n]);

		// \0を抜かす
		/*s=ss;
		p=wi.js;
		do {
			while (*s) *p++=*s++;
			s++;
		} while (*s);
		*p=0;*/

		ks->echoStr=(uint8_t*)wi.js;
		ks->length=strlen(wi.js);
#else
		kconvert(ks);
#endif
		return 0;

	case CANNA_KEY_Left:
		ks->echoStr=(uint8_t*)wi.js;
		ks->length=strlen(wi.js);

		if (wi.kanji>0) {
			// 左の文節へ
			wi.seg--;
			if (wi.seg<0) wi.seg=whiz.seg-1;
			wi.cand=1;

			n=0;
			//for (i=0; i<wi.seg; i++) n+=strlen(whiz.get_word(whiz.p[whiz.seg -i -1]));
			for (i=0; i<wi.seg; i++) n+=strlen(wi.kan[i]);
			wi.revPos = ks->revPos = n;
			//wi.revLen = ks->revLen = strlen(whiz.get_word(whiz.p[whiz.seg -wi.seg -1]));
			wi.revLen = ks->revLen = strlen(wi.kan[wi.seg]);
			return 0;
		} else {
			// 左へ
			if (wi.revPos!=0) {
				//wi.revPos--;
				wi.revPos-=2;
				if (wi.revPos < 0) wi.revPos=0;
				ks->revPos=wi.revPos;
				return 0;
			}
			*buff=ch;
			buff[1]=0;
			ks->info |= KanjiThroughInfo;
			return 1;
		}

	case CANNA_KEY_Right:
		ks->echoStr=(uint8_t*)wi.js;
		ks->length=strlen(wi.js);

		if (wi.kanji>0) {
			// 右の文節へ
			wi.seg++;
			if (wi.seg>=whiz.seg) wi.seg=0;
			wi.cand=1;

			n=0;
			//for (i=0; i<wi.seg; i++) n+=strlen(whiz.get_word(whiz.p[whiz.seg -i -1]));
			for (i=0; i<wi.seg; i++) n+=strlen(wi.kan[i]);
			wi.revPos = ks->revPos = n;
			//wi.revLen = ks->revLen = strlen(whiz.get_word(whiz.p[whiz.seg -wi.seg -1]));
			wi.revLen = ks->revLen = strlen(wi.kan[wi.seg]);
			return 0;
		} else {
			// 右へ
			/*if (wi.revPos >= strlen(wi.s)) {
				wi.revPos++;
				if (wi.revPos >= ks->length) wi.revPos=ks->length;
				ks->revPos=wi.revPos;
				return 0;
			}*/
			*buff=ch;
			buff[1]=0;
			ks->info |= KanjiThroughInfo;
			return 1;
		}

	case CANNA_KEY_Shift_Left:
		if (wi.kanji>0) {
			// 文節縮め
			n=whiz.resize(wi.seg, -2);
			//whiz.reconvert(ss, n, wi.seg);
			whiz.reconvert(ss, n, 0);
			//ks->revLen=strlen(ss);
			//wi.seg=0;
			wi.cand=1;
			//wi.revPos=0;
			//wi.revLen=ks->revLen;

			// 配列にコピー
			n=0;
			s=ss;
			do {
				p=wi.kan[n++];
				while (*s) *p++=*s++;
				*p=0;
				s++;
			} while (*s);
			// 現在の文節を指定
			n=0;
			for (i=0; i<wi.seg; i++) n+=strlen(wi.kan[i]);
			wi.revPos = ks->revPos = n;
			wi.revLen = ks->revLen = strlen(wi.kan[wi.seg]);
			// 表示用
			wi.js[0]=0;
			for (n=0; n<whiz.seg; n++) strcat(wi.js, wi.kan[n]);

			ks->echoStr=(uint8_t*)wi.js;
			ks->length=strlen(wi.js);
			return 0;
		} else {
			*buff=ch;
			buff[1]=0;
			ks->info |= KanjiThroughInfo;
			return 1;
		}

	case CANNA_KEY_Shift_Right:
		if (wi.kanji>0) {
			// 文節伸ばし
			n=whiz.resize(wi.seg, -1);
			//whiz.reconvert(ss, n, wi.seg);
			whiz.reconvert(ss, n, 0);
			//ks->revLen=strlen(ss);
			wi.cand=1;
			//wi.revPos=0;
			//wi.revLen=ks->revLen;

			// 配列にコピー
			n=0;
			s=ss;
			do {
				p=wi.kan[n++];
				while (*s) *p++=*s++;
				*p=0;
				s++;
			} while (*s);
			// 現在の文節を指定
			n=0;
			for (i=0; i<wi.seg; i++) n+=strlen(wi.kan[i]);
			wi.revPos = ks->revPos = n;
			wi.revLen = ks->revLen = strlen(wi.kan[wi.seg]);
			// 表示用
			wi.js[0]=0;
			for (n=0; n<whiz.seg; n++) strcat(wi.js, wi.kan[n]);

			ks->echoStr=(uint8_t*)wi.js;
			ks->length=strlen(wi.js);
			return 0;
		} else {
			*buff=ch;
			buff[1]=0;
			ks->info |= KanjiThroughInfo;
			return 1;
		}

	case CANNA_KEY_Help:
		// ESC(取り消し)
		if (wi.kanji>0) {
			// 変換中
			wi.kanji=0;
			ch=0;
			break;
		}
		wi.s[0]=0;
		wi.js[0]=0;
		ks->echoStr=(uint8_t*)wi.js;
		ks->length=0;
		return 0;

	case CANNA_KEY_F6:
		// ひらがな
		if (strlen(wi.s)>0) {
			kana(ss, wi.s);
			ks->echoStr=(uint8_t*)/*wi.js*/ss;
			ks->length=strlen(/*wi.js*/ss);
		} else {
			wi.mode=get_canna_mode(CANNA_MODE_ZenHiraHenkanMode);
			ks->info|=KanjiModeInfo;
		}
		return 0;

	case CANNA_KEY_F7:
		// 片仮名
		if (strlen(wi.s)>0) {
			kana(ss, wi.s);
			kana2kata(wi.js, ss);
			ks->echoStr=(uint8_t*)wi.js;
			ks->length=strlen(wi.js);

			wi.kanji=1; // 確定処理
			return ks->length;
		} else {
			wi.mode=get_canna_mode(CANNA_MODE_ZenKataHenkanMode);
			ks->info|=KanjiModeInfo;
		}
		return 0;

	case CANNA_KEY_F8:
		// 半角片仮名
		if (strlen(wi.s)>0) {
			kana(ss, wi.s);
			kana2half_kata(wi.js, ss);
			ks->echoStr=(uint8_t*)wi.js;
			ks->length=strlen(wi.js);

			wi.kanji=1; // 確定処理
			return ks->length;
		} else {
			wi.mode=get_canna_mode(CANNA_MODE_HanKataHenkanMode);
			ks->info|=KanjiModeInfo;
		}
		return 0;

	case CANNA_KEY_F9:
		// 全角ローマ字
		if (strlen(wi.s)>0) {
			alpha2zen_a(wi.js, wi.s);
			ks->echoStr=(uint8_t*)wi.js;
			ks->length=strlen(wi.js);

			wi.kanji=1; // 確定処理
			return ks->length;
		} else {
			wi.mode=get_canna_mode(CANNA_MODE_ZenAlphaHenkanMode);
			ks->info|=KanjiModeInfo;
		}
		return 0;

	case CANNA_KEY_F10:
		// ローマ字
		if (strlen(wi.s)>0) {
			strcpy(wi.js, wi.s);
			ks->echoStr=(uint8_t*)wi.js;
			ks->length=strlen(wi.js);

			wi.kanji=1; // 確定処理
			return ks->length;
		} else {
			wi.mode=get_canna_mode(CANNA_MODE_HanAlphaHenkanMode);
			ks->info|=KanjiModeInfo;
		}
		return 0;

	/*case CANNA_KEY_Insert:
	case CANNA_KEY_Home:
	case CANNA_KEY_End:
		*buff=ch;
		buff[1]=0;
		ks->info |= KanjiThroughInfo;
		return 1;*/

	/*case 0x09:		// Tab
	case 0x13:		// Pause
	case 0x14:		// Scroll Lock
	case 0x15:		// Sys Req
	//case 0xff:		// Del
	//case 0x57:		// End
	case 0x7f:		// Del*/
	default:
		// 特殊文字を処理する
		if ((ch >= 0 && ch <= 0x1f) || (ch >= 0x7f && ch <= 0xff)) {
			*buff=ch;
			buff[1]=0;
			ks->info |= KanjiThroughInfo;
			return 1;
		}
	}

	if (wi.kanji>0) {
		// 変換中-文字列を確定しコピー
		strcpy(buff, wi.js);
		wi.kanji=0;
		wi.s[0]=ch;
		wi.s[1]=0;
		n=strlen(buff);
	} else {
		// 未変換部分を追加(ローマ字で)
		n=strlen(wi.s);
		wi.s[n++]=ch;
		wi.s[n]=0;
		n=0;
	}

	// かなへ変換
	kana(wi.js, wi.s);

	switch (wi.mode) {
	case 1:
		// 片仮名
		char s[LINEMAX];
		strcpy(s, wi.js);
		kana2kata(wi.js, s);
		break;
	case 2:
		strcpy(s, wi.js);
		kana2half_kata(wi.js, s);
		break;
	}

	// 未変換文字として追加(かな)
	ks->echoStr=(uint8_t*)wi.js;
	ks->length=strlen(wi.js);
	wi.revPos = ks->revPos = ks->length;
	//ks->revPos=0;
	ks->revLen=0;

//	strcpy(wi.s, wi.js); // かなへ

	return n;
}

}
