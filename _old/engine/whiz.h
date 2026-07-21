//---------------------------------------------------------
//	Whiz (Japanese Input Method Engine)
//
//		©2003-2023 Yuichiro Nakada
//---------------------------------------------------------

#ifndef __whiz_conf__
#define __whiz_conf__


// Version
#define WHIZ_VER	"0.62"				// Version
#define WHIZ_CODE	"Virgo"				// Code Name

// Directories
#define WHIZ_DIR	"/opt/whiz"			// Install dir
#define WHIZ_DIC	WHIZ_DIR "/dic/"		// Dictionaries dir
//#define WHIZ_ETC	WHIZ_DIR "/etc"			// Etc dir

// Define Dictionaries
#define WHIZDIC		WHIZ_DIC "whiz.dic"		// Words Dictionaries
#define WHIZINX		WHIZ_DIC "whiz.inx"		// Index of Words Dictionaries

#define GRAMMARDIC	WHIZ_DIC "grammar.dic"		// Word Class Dictionary

#define CONNECTDIC	WHIZ_DIC "connect.dic"		// Connection Dictionary
#define CONNECTINX	WHIZ_DIC "connect.inx"		// Index of Connection Dictionary

#define FORMSDIC	WHIZ_DIC "forms.dic"		// Forms Dictionary

#define LEARNDIC	"/.whiz/learn.dic"		// Word Learning Dictionary

// Dictionary Type
#define BINARYDIC					// Use Binary Dictionary
#define BINARYINX					// Use Binary Index
#define WORDLEARN					// Use Word Learning
//#define LEARNFREQ	1


// for Whiz
#define LINEMAX		8192
#define WORDMAX		100

#define REFMAX		3000				// Max of Refernce
#define PATHMAX		50000				// Max of Path

#define TYPESMAX	75				// 最大活用型数
#define FORMSMAX	30				// 最大活用形数

//#define ANALYSISMAX	30				// 最大解析数
#define MAXSENTENCE	50				// 保存しておく候補数
#define MAXSEGMENT	100				// 最大分節数


// for Debug
//#define DEBUG
#define SYSLOG


// for Server and Client
#define USE_UNIX_SOCKET
//#define USE_INET_SOCKET

#define IR_UNIX_DIR		"/tmp/.iroha_unix"
#define IR_UNIX_SOCKNAME	"IROHA"
#define IR_UNIX_PATH		IR_UNIX_DIR "/" IR_UNIX_SOCKNAME
#define IR_SERVICE_NAME		"canna"
#define IR_DEFAULT_PORT		5680

#define PIDPATH			"/tmp/"
#define WHIZ_PID_PATH		PIDPATH "/whiz.pid"

#define MAXSOCK			8			// 最大ソケット数(最大32)


#define OP_SYSLOG		1
#define OP_INET			2


#ifdef __cplusplus
#include "whizdic.h"
//---------------------------------------------------------
//	Whiz Class
//---------------------------------------------------------

class WHIZ {
public:
	//DIC_INFO di;
	//PATH_INFO pi;
	//CONNECT_INFO cinfo;
	//CONNECT con[12700];
	//PATH path[PATHMAX];
	//DIC dic[REFMAX];
	//INFLECT inf[80];

	int seg;		// 文節数
	int p[MAXSEGMENT];	// 現在のパスを保存

	// 初期化
	int initialize();

	// 日本語解析
	int analysis(char *s);

	// 変換候補の取得
	int convert(char *s);
	int reconvert(char *s, int f, int m);

	// 区切りを変更
	int resize(int n, int f);

	// 候補を取得
	int get_candidacy(int n, char *s, int &c, DIC **dd=0);

	// その文節までの文字数を調べる
	int get_segment(int n, int f=0);

#ifdef WORDLEARN
	void set_learndic(char *dir, const char *name);
	// 学習辞書に使用頻度を記録
	void learning(DIC *d);
#endif

	// 表示
	void print();

	char *get_read(int n);
	char *get_word(int n);
};
#endif	// __cplusplus


#endif	// __whiz_conf__
