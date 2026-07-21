//---------------------------------------------------------
//	Whiz Dic (Japanese Input Method Engine)
//
//		(C)2003,2007 NAKADA
//---------------------------------------------------------

//#include "whiz.h"


//---------------------------------------------------------
//	Structure
//---------------------------------------------------------

// 単語辞書
struct DIC {
	short cost;		// コスト (0-4000)
	short wclass;		// 品詞 (0-91)
	short type;		// 活用型
	short form;		// 活用形

	//char *read;
	//char *word;
	char read[WORDMAX];	// 読み
	char word[WORDMAX];	// 単語
	char org[WORDMAX];	// 原型

	char len;		// 読みの長さ (WORDMAX=100)
	int num;		// 登録番号
	char freq;		// 参照頻度 (単語学習)
};

// 単語辞書情報
class DIC_INFO {
public:
	int num;		// 総数(number)
	int f[LINEMAX];		// もう検索した？
	int c[LINEMAX];		// 検索数

	// 初期化
	void init()
	{
		int i;
		for (i=LINEMAX-1; i>=0; i--) f[i]=-1;
		num=0;
	}
};

// 連接辞書
struct CONNECT {
	int cost;		// コスト
	int wclass[2];		// 品詞
	int type[2];		// 活用型
	int form[2];		// 活用形
	char word[2][WORDMAX];	// 語彙化品詞定義
};

// 連接情報
struct CONNECT_INFO {
	int wclass[90][90];
};

// 活用辞書
struct INFLECT {
	// 活用型
	char type[30];

	int form[FORMSMAX];		// 活用形
	char word[FORMSMAX][30];	// 語尾
	char read[FORMSMAX][30];	// 読み
	char pro[FORMSMAX][30];		// 発音
};

// パス
struct PATH {
	DIC *d;			// Pointer of Dictionary

	int b;			// Back Path
	int n;			// Next Path

	int nc;			// Next Choise

	int cost;		// Cost
	int tc;			// Total Cost

	int len;
	int flag;
};

// パス情報
class PATH_INFO {
public:
	int num;		// Number of path

	int sn;			// Number of sentence
	PATH *s[MAXSENTENCE];	// Sentence

	//int cost[WORDMAX/2];

	void init()
	{
		int i;
		num=sn=0;
		//for (i=0; i<WORDMAX/2; i++) cost[i]=0;
		for (i=MAXSENTENCE-1; i>=0; i--) s[i]=0;
	}
	int adds(PATH *p)
	{
		// add sentence
		//s[sn++]=p;
		//if (sn>=50) { printf("Error: sentence overflow !!"); sn=0; return 1; }

		int i;
		for (i=0; i<sn; i++) {
			if (s[i]->tc > p->tc) {
				PATH *a;
				a=s[i];
				s[i]=p;
				p=a;
			}
		}
		s[sn++]=p;

		if (sn>=MAXSENTENCE) { printf("Error: sentence overflow !!"); sn=MAXSENTENCE-1; return 1; }

		return 0;
	}
	void sentence();
};
