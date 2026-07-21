//---------------------------------------------------------
//	Whiz Server (Japanese Input Method Engine)
//
//		©2003-2023 Yuichiro Nakada
//---------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include "engine/whiz.h"
#include "debug/debug.h"


//---------------------------------------------------------
//	Global Variable
//---------------------------------------------------------

WHIZ whiz;		// 仮名漢字変換エンジン
int option;		// オプション


//---------------------------------------------------------
//	Read from socket functions
//---------------------------------------------------------

int socket_read(int fd, void *vp, int ts)
{
	char *p;
	int size, r;

	p=(char*)vp;
	size = 0;
	while (size < ts) {
		if ((r = read(fd, &p[size], ts - size)) <= 0) return -1;
		size += r;
	}

	return 0;
}


//---------------------------------------------------------
//	Write to socket functions
//---------------------------------------------------------

int socket_write(int fd, void *vp, int ts)
{
	char *p;
	int size, r;

	p=(char*)vp;
	size = 0;
	while (size < ts) {
		if ((r = write(fd, &p[size], ts - size)) < 0) return -1;
		size += r;
	}

	return 0;
}


//---------------------------------------------------------
//	UNIXドメイン作成
//---------------------------------------------------------

#ifdef USE_UNIX_SOCKET
int canna_unixfd;
struct sockaddr_un unsock;

int canna_socket_open_unix(int port)
{
	int old_umask;
	int request;

	unsock.sun_family = AF_UNIX;
	old_umask = umask(0);

	// /tmp/.iroha_unix を作成
	if (mkdir(IR_UNIX_DIR, 0777) == -1 && errno != EEXIST ) {
		smsg(D_ERR, "Can't open %s error No. %d\n", IR_UNIX_DIR, errno);
	}

	// /tmp/.iroha_unix/IROHA:x を作成
	strcpy(unsock.sun_path, IR_UNIX_PATH);
	if (port) snprintf(unsock.sun_path, sizeof(unsock.sun_path), "%s:%d", unsock.sun_path, port);

//#ifdef DEBUG
	unlink(unsock.sun_path);
//#endif

	// Unix ソケットを作成
	if ((request = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		smsg(D_WARN, "Warning: UNIX socket for server failed.\n");
	} else {
		if (bind(request, (struct sockaddr *)&unsock, strlen(unsock.sun_path) + 2) == 0) {
			debug(dmsg(D_INFO, "Soket Filename: %s (%d)\n", unsock.sun_path, request);)
			if (listen(request, 5)) {
				smsg(D_WARN, "Warning: Server could not listen.\n");
				close(request);
				request = -1; /* listen 失敗 */
			}
		} else {
			smsg(D_WARN, "Warning: Server could not bind.\n");
			close(request);
			request = -1; /* bind 失敗 */
		}
	}
	(void)umask(old_umask);

	return request;
}
#endif /* use_unix_socket */


//---------------------------------------------------------
//	INETドメイン作成
//---------------------------------------------------------

#ifdef USE_INET_SOCKET
int canna_inetfd;
struct sockaddr_in insock;

int canna_socket_open_inet(int port)
{
	struct servent *s;
	int one = 1, fd, i;

	// Inet ソケットを作成
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		smsg(D_ERR, "Cannot open inet domain socket.\n");
		return -1;
	}

#ifdef SO_REUSEADDR
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)(&one), sizeof(int));
#endif

	// /etc/servicesからポート番号を取得する
	s = getservbyname(IR_SERVICE_NAME, "tcp");

	memset((char*)&insock, 0, sizeof(insock));
	insock.sin_family = AF_INET;
	insock.sin_port = (s ? s->s_port : htons(IR_DEFAULT_PORT)) + port;
	insock.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(fd, (struct sockaddr *)(&insock), sizeof(insock)) != 0) {
		smsg(D_WARN, "Warning: Server could not bind.\n");
		return -1;
	}

	if (listen(fd, 5)) {
		close(fd);
		smsg(D_WARN, "Warning: Server could not listen.\n");
		return -1;
	}

	return fd;
}
#endif /* use_inet_socket */


struct INFO_CONNECTION {
	int s;			// FD of Soket

	char user[10];		// User Name
	char host[256];		// Host Name
	char homedir[256];	// Path of Home Directory
};

INFO_CONNECTION ic[MAXSOCK+2];
//---------------------------------------------------------
//	接続準備
//---------------------------------------------------------

int canna_socket_open()
{
	int i;

	// 初期化
	//for (i=0; i<=MAXSOCK; i++) ic[i].s=0;
	for (i=0; i<=MAXSOCK; i++) ic[i].s=-1;

	// 接続
#ifdef USE_UNIX_SOCKET
	if ((canna_unixfd = canna_socket_open_unix(0)) == -1) {
		debug(dmsg(D_WARN, "unix domain not created.\n");)
		return -1;
	}
	ic[0].s=canna_unixfd;
#endif
#ifdef USE_INET_SOCKET
	if (!(option & OP_INET)) return WS_SUCCESS;
	if ((canna_inetfd = canna_socket_open_inet(0)) == -1) {
		debug(dmsg(D_WARN, "inet domain not created.\n");)
		return -1;
	}
	ic[1].s=canna_inetfd;
#endif

	return 0;
}


//---------------------------------------------------------
//	接続を閉じる
//---------------------------------------------------------

int canna_socket_close()
{
#ifdef USE_UNIX_SOCKET
	// UNIXドメインを閉じる
	close(canna_unixfd);
	unlink(unsock.sun_path);
#endif

#ifdef USE_INET_SOCKET
	// INETドメインを閉じる
	close(canna_inetfd);
#endif

	return 0;
}


pid_t child;
char *pid_file_path;
//---------------------------------------------------------
//	親を終了させる
//---------------------------------------------------------

void quit_parent()
{
	FILE *fp;

	if (pid_file_path != NULL && (fp = fopen(pid_file_path, "w")) != NULL) {
		fprintf(fp, "%d\n", child);
		fclose(fp);
	}

	exit(0);
}


//---------------------------------------------------------
//	デーモンになるっ
//---------------------------------------------------------

int daemonize(const char *pid_path)
{
//	pid_t parent;
#ifndef DEBUG
	int fd;
#endif

	pid_file_path = (char*)pid_path;

	// 親のPID
//	parent = getpid();

#ifndef DEBUG
	// 標準入出力を切り離す
	close(fileno(stdin));
	close(fileno(stdout));
#endif

	if ((child = fork()) == -1) {
		debug(dmsg(D_ERR, "Fork faild.\n");)
		return EOF;
	}

	if (child) quit_parent();

	signal(SIGHUP, SIG_IGN);

	setsid();

#ifndef DEBUG
	close(fileno(stderr));

	if ((fd = open("/dev/null", 2)) >= 0) {
		if (fd != 0) dup2(fd, 0);
		if (fd != 1) dup2(fd, 1);
		if (fd != 2) dup2(fd, 2);
		if (fd != 0 && fd != 1 && fd != 2) close(fd);
	}
#endif

	chdir("/");

	return 0;
}


//---------------------------------------------------------
//	サーバーの終了処理
//---------------------------------------------------------

inline void term_all(const char *msg)
{
	canna_socket_close();
	debug(dmsg(D_INFO, msg);)
}


//---------------------------------------------------------
//	SIGNALによる終了
//---------------------------------------------------------

inline void sig_terminate()
{
	term_all("Terminated by signal.\n");
}


//---------------------------------------------------------
//	SIGNALの設定
//---------------------------------------------------------

int setup_signal(sighandler_t handler)
{
	signal(SIGINT, (sighandler_t)handler);
	signal(SIGTERM, (sighandler_t)handler);
	signal(SIGHUP, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	return 0;
}


//---------------------------------------------------------
//	定義
//---------------------------------------------------------

#define L4TOL(l4) \
	((((((((unsigned long) ((unsigned char)(l4)[0])) << 8) | \
		((unsigned long) ((unsigned char)(l4)[1]))) << 8) | \
		((unsigned long) ((unsigned char)(l4)[2]))) << 8) | \
		((unsigned long) ((unsigned char)(l4)[3])))
#define S2TOS(s2) \
	((unsigned short)(((unsigned char)(s2)[0]<<8) | (unsigned char)(s2)[1]))

#define LOMASK(x)	((x)&255)
#define LTOL4(l, l4)	{ \
	(l4)[0] = LOMASK((l)>>24); (l4)[1] = LOMASK((l)>>16); \
	(l4)[2] = LOMASK((l)>> 8); (l4)[3] = LOMASK((l)); \
}

#define LSBMSB16(_s) ((((_s) >> 8) & 0xff) | (((_s) & 0xff) << 8))
#define LSBMSB32(_s) ((((_s) >> 24) & 0xff) | (((_s) & 0xff) << 24) | \
                      (((_s) >> 8) & 0xff00) | (((_s) & 0xff00) << 8))

/* Whiz Server Messages */
#define WS_ERROR	-1	// IMEとの接続が落ちた
#define WS_SUCCESS	0
#define WS_UNKNOWN	1
#define WS_TERMINATE	2
#define WS_OK_NO_SEND	4

#define READ_SIZE 	8192
#define EXTBASEPROTONO	0x00010000
#define MAXREALREQUEST	0x24


struct cannaheader_t {
	unsigned char type;		// Request Type
	unsigned char flag;		// Extended Flag
	unsigned short len;		// Data Length

	union {
		unsigned short e16;	// Error Flag for 16Bit
		unsigned char e8;	// Error Flag for 8Bit
	} err;
};

struct REQUEST {
	cannaheader_t *h;		// Canna Header
	char *req;			// Request
	int size;			// Size of Request

	INFO_CONNECTION *ic;		// Infomation of Connection
};


//---------------------------------------------------------
//	EUCをかんなで使われるワイドキャラクタに変換
//---------------------------------------------------------

int convert_wcs(char *p, char *s, int len)
{
	int n, m;

	m=n=0;
	while (m<len) {
		if (*s>=0x20 && *s<=0x7e) {
			// ASCII文字 (0x20〜0x7e)
			*p++=0;
			*p++=*s++;
			m++;
		} else if ((unsigned char)*s>=0xa1 && (unsigned char)*s<=0xfe) {
			// 漢字 (第1,2バイトとも 0xa1〜0xfe)
			*p++=*s++;
			*p++=*s++;
			m+=2;
		} else {
			// 制御コード (0x00〜0x1f,0x7f)
			// 半角片仮名 (0x8ea1〜0x8edf)
			// 補助漢字 (0x8fa1a1〜0x8ffefe)
			s++;
			*p++=0;
			*p++=0;
			m++;
		}
		n++;
	}

	return n*2;
}


//---------------------------------------------------------
//	ワイドキャラクタからEUCに変換
//---------------------------------------------------------

int convert_euc(char *p, char *s, int len)
{
	int n, m;

	m=n=0;
	while (m<len) {
		if (*s) {
			*p++=*s;
			n++;
		}
		s++;
		*p++=*s++;
		n++;
		m+=2;
	}

	return n;
}


//---------------------------------------------------------
//	エラー処理
//---------------------------------------------------------

int whiz_error(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_error !! >>\n");)
	smsg(D_ERR, "<< whiz_error !! >>\n");
	r->size=0;
	return WS_SUCCESS;
}


//---------------------------------------------------------
//	サーバー初期化
//---------------------------------------------------------

int whiz_initialize(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_initialize !! >>\n" );)

	// プロトコルバージョンのチェック
	char *p;
	p=strchr(r->req+8, ':');				// ex "3.3:root"
	*p++=0;
	debug(dmsg(10, "Protocol Version '%s'\n", r->req+8);)	// バージョン番号
	debug(dmsg(10, "User Name '%s'\n", p);)			// ユーザ名
	strcpy(r->ic->user, p);

#ifdef WORDLEARN
	if (strcmp(p, "root")) {
		strcpy(r->ic->homedir, "/home/");
		strcat(r->ic->homedir, p);
	} else {
		strcpy(r->ic->homedir, "/root");
	}
	whiz.set_learndic(r->ic->homedir, LEARNDIC);
#endif

	// 結果
	LTOL4(0x30001, r->req);			// サーバマイナーバージョン(2)+コンテキスト番号(2)
	r->size=4;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	サーバー終了処理
//---------------------------------------------------------

int whiz_finalize(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_finalize !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	コンテキストを作る
//---------------------------------------------------------

int whiz_create_context(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_create_context !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(2);		// コンテキスト
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	コンテキストを複製する
//---------------------------------------------------------

int whiz_duplicate_context(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_duplicate_context !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+4));)	// コンテキスト

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(3);		// コンテキスト
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	コンテキストを削除する
//---------------------------------------------------------

int whiz_close_context(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_close_context !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+4));)	// コンテキスト

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	辞書テーブル一覧
//---------------------------------------------------------

int whiz_get_dictionary_list(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_dictionary_list !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+4));)	// コンテキスト
	debug(dmsg(10, "Buff size %d\n", S2TOS(r->req+6));)	// バッファサイズ

	//r->h->len=LSBMSB16(2);			// err16
	//r->h->err.e16=LSBMSB16(-1);		// 失敗
	//r->size=6;

	r->h->len=LSBMSB16(12);			// データ
	r->h->err.e16=LSBMSB16(1);		// 辞書数
	strcpy(r->req+6, "whiz.dic\0");
	r->size=16;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	辞書ディレクトリ一覧
//---------------------------------------------------------

int whiz_get_directory_list(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_directory_list !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+4));)	// コンテキスト
	debug(dmsg(10, "Buff size %d\n", S2TOS(r->req+6));)	// バッファサイズ

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// 失敗
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	辞書リスト追加
//---------------------------------------------------------

int whiz_mount_dictionary(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_mount_dictionary !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+8));)	// コンテキスト
	debug(dmsg(10, "Dic Name '%s'\n", r->req+10);)		// 辞書名

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	辞書リスト追加
//---------------------------------------------------------

int whiz_umount_dictionary(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_umount_dictionary !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+8));)	// コンテキスト
	debug(dmsg(10, "Dic Name '%s'\n", r->req+10);)		// 辞書名

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	辞書リスト変更
//---------------------------------------------------------

int whiz_remount_dictionary(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_remount_dictionary !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+8));)	// コンテキスト
	debug(dmsg(10, "Dic Name '%s'\n", r->req+10);)		// 辞書名

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	辞書リスト一覧
//---------------------------------------------------------

int whiz_get_mount_dictionary_list(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_mount_dictionary_list !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+4));)	// コンテキスト
	debug(dmsg(10, "Buff size %d\n", S2TOS(r->req+6));)	// バッファサイズ

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// 失敗
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	登録可能辞書の問い合わせ
//---------------------------------------------------------

int whiz_query_dictionary(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_query_dictionary !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=(unsigned char)-1;		// 失敗
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	単語登録
//---------------------------------------------------------

int whiz_define_word(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_define_word !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=(unsigned char)-1;		// 失敗
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	単語削除
//---------------------------------------------------------

int whiz_delete_word(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_delete_word !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=(unsigned char)-1;		// 失敗
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	変換開始
//---------------------------------------------------------

int whiz_convert(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_convert !! >>\n");)

	int n;
	char buff[4096];
	//whiz.analysis(r->req+10);
	convert_euc(buff, r->req+10, r->size-10);
	debug(dmsg(10, "Input <%s>\n", buff);)
	whiz.analysis(buff);
	debug(whiz.print();)

	n=whiz.convert(buff);			// 文節1 \0 文節2 \0\0
	n=convert_wcs(r->req+6, buff, n);	// 文節1 \0\0 文節2 \0\0 \0\0

	r->h->len=LSBMSB16(2+n);		// err16 + n
	r->h->err.e16=LSBMSB16(whiz.seg);	// 文節数
	r->size=6+n;

	debug(
		char *dp;
		dp=buff;
		dmsg(10, "Output <");
		do {
			fprintf(stderr, "%s ", dp);
			while (*dp) dp++;
			dp++;
		} while (*dp);
		fprintf(stderr, ">\n");
	)
	debug(dmsg(10, "len=%d segment=%d\n", n, whiz.seg);)

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	変換終了
//---------------------------------------------------------

int whiz_convert_end(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_convert_end !! >>\n");)

#ifdef WORDLEARN
	debug(dmsg(D_INFO, "word learning: %d -seg:%d-\n", L4TOL(r->req+8), S2TOS(r->req+6));)
	if (L4TOL(r->req+8)) {	// 0 なら学習しない
		int seg = S2TOS(r->req+6);
		for (int i=0; i<seg; i++) {
			int cc = S2TOS(r->req+12+i*2);
			debug(dmsg(D_INFO, " checking -seg:%d cand:%d-\n", i, cc);)
			//if (cc) whiz.learning((DIC*)whiz.get_candidacy(i, 0, cc));
			if (cc) {
				DIC *d;
				whiz.get_candidacy(i, 0, cc, &d);
				whiz.learning(d);
			}
		}
	}
#endif

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	候補要求
//---------------------------------------------------------

int whiz_get_candidacy_list(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_candidacy_list !! >>\n");)

	int c, len;
	char buff[4096];
	len=whiz.get_candidacy(S2TOS(r->req+6), buff, c);	// 候補\0 候補\0 読み\0\0 (r->req+6 カレント文節番号)
	debug(dmsg(D_INFO, "current seg: %d, count:%d <%s...(len:%d)>\n", S2TOS(r->req+6), c, buff, len);)
	len=convert_wcs(r->req+6, buff, len);			// 候補\0\0 候補\0\0 読み\0\0\0\0

	r->h->len=LSBMSB16(2+len);		// err16 + len
	r->h->err.e16=LSBMSB16(c);		// 候補数
	r->size=6+len;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	読みがな取得
//---------------------------------------------------------

int whiz_get_yomi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_yomi !! >>\n");)

	int n, m;
	n=S2TOS(r->req+6);
	m=strlen(whiz.get_read(whiz.p[whiz.seg-n-1]));
	debug(dmsg(10, "Segmnt %d, %s(%d)\n", n, whiz.get_read(whiz.p[whiz.seg-n-1]), m);)

	r->h->len=LSBMSB16(2+m+4);		// err16 + len + 4 (EOS)
	r->h->err.e16=LSBMSB16(m/2);		// 読みの長さ
	strncpy(r->req+6, whiz.get_read(whiz.p[whiz.seg-n-1]), m);
	r->req[m+6]=0;
	r->req[m+7]=0;
	r->req[m+8]=0;
	r->req[m+9]=0;
	r->size=6+m+4;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	自動変換
//---------------------------------------------------------

int whiz_subst_yomi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_subst_yomi !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// 失敗
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	読みがな変更
//---------------------------------------------------------

int whiz_store_yomi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_store_yomi !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// 失敗
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	カレント分節のみの単文節変換
//---------------------------------------------------------

int whiz_store_range(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_store_range !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// 失敗
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	未決分節取得
//---------------------------------------------------------

int whiz_get_lastyomi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_lastyomi !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// 失敗
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	強制変換
//---------------------------------------------------------

int whiz_flush_yomi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_flush_yomi !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// 失敗
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	読みバッファ削除
//---------------------------------------------------------

int whiz_remove_yomi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_remove_yomi !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=(unsigned char)-1;		// 失敗
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	限定候補取得
//---------------------------------------------------------

int whiz_get_simplekanji(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_simplekanji !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// 失敗
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	区切り変更
//---------------------------------------------------------

int whiz_resize_pause(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_resize_pause !! >>\n");)

	int n;
	char buff[4096];

	short s;
	s=S2TOS(r->req+8);				// 読みの長さ(-2文節縮め/-1文節伸ばし)
	if (s>=0) {
		s*=2;					// 全角で１文字
		n=S2TOS(r->req+6);			// 文節番号
		n=strlen(whiz.get_read(whiz.p[whiz.seg-n-1]));
		debug(dmsg(10, "len %d<->%d\n", n, s);)
		if (s>=n) s=-1;
		else s=-2;
	}
	n=whiz.resize(S2TOS(r->req+6), s);		// 文節番号
	n=whiz.reconvert(buff, n, S2TOS(r->req+6));	// 文節1 \0 文節2 \0\0
	//n=convert_wcs(buff_, buff, n);			// 文節1 \0\0 文節2 \0\0 \0\0
	n=convert_wcs(r->req+6, buff, n);		// 文節1 \0\0 文節2 \0\0 \0\0
	debug(dmsg(10, "len=%d segment=%d\n", n, whiz.seg);)

	r->h->len=LSBMSB16(2+n);		// err16 + n
	r->h->err.e16=LSBMSB16(whiz.seg);	// 文節数
	r->size=6+n;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	品詞情報
//---------------------------------------------------------

int whiz_get_hinshi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_hinshi !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// 失敗
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	形態素情報
//---------------------------------------------------------

int whiz_get_lex(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_lex !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// 失敗
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	解析情報
//---------------------------------------------------------

int whiz_get_status(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_status !! >>\n");)

	r->h->len=LSBMSB16(1+28);		// err8 + 28 (stat)
	r->h->err.e8=0;				// OK !!

	struct stat {
		int bunnum;	// 文節番号
		int candnum;	// 候補番号
		int maxcand;	/* カレント文節の候補数 */
		int diccand;	/* FIXME: maxcand - モード指定分 */
		int ylen;	/* カレント候補の読みがなのバイト数 */
		int klen;	/* カレント候補の漢字候補のバイト数 */
		int tlen;	/* カレント候補の構成単語数 */
	};
	stat *s;
	s=(stat*)(r->req+5);

	int cc = S2TOS(r->req+8);		// カレント候補番号
	//DIC *d = (DIC*)whiz.get_candidacy(S2TOS(r->req+6), 0, cc);	// 候補数を取得
	DIC *d;
	whiz.get_candidacy(S2TOS(r->req+6), 0, cc, &d);	// 候補数を取得
	/*char buff_[4096];
	//smsg(10, "<< whiz_get_status !! >>\n%d\n", S2TOS(r->req+6));
	whiz.get_candidacy(S2TOS(r->req+6), buff_, cc);	// 候補数を取得
	//smsg(10, "cand %d\n", cc);*/

	s->maxcand = LSBMSB32(cc);		// 候補数
	s->diccand = LSBMSB32(cc);		// 候補数
	cc=S2TOS(r->req+6);
	s->bunnum = LSBMSB32(cc);		// カレント文節番号
	s->candnum = LSBMSB32(S2TOS(r->req+8));	// カレント候補番号
	/*s->ylen=strlen(whiz.get_read(whiz.p[whiz.seg - cc - 1]));
	s->klen=strlen(whiz.get_word(whiz.p[whiz.seg - cc - 1]));
	s->ylen=LSBMSB32(s->ylen);
	s->klen=LSBMSB32(s->klen);*/
	s->ylen=LSBMSB32(strlen(d->read));
	s->klen=LSBMSB32(strlen(d->word));
	s->tlen=LSBMSB32(1);
	//smsg(D_INFO, "cand %d, %s\n", cc, whiz.get_word(whiz.p[whiz.seg - cc - 1]));

	debug(dmsg(10, "seg %d, cand %d, read %s, word %s\n", LSBMSB32(s->bunnum), LSBMSB32(s->candnum), whiz.get_read(whiz.p[whiz.seg - LSBMSB32(s->bunnum) - 1]), whiz.get_word(whiz.p[whiz.seg - LSBMSB32(s->bunnum) - 1]));)
	debug(dmsg(10, "maxcand %d, yomi_len %d, kanji_len %d\n", LSBMSB32(s->maxcand), LSBMSB32(s->ylen), LSBMSB32(s->klen));)

	//socket_write(r->ic->s, &stat, 28);
	r->size=5+28;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	Locale情報設定
//---------------------------------------------------------

int whiz_set_locale(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_set_locale !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// 成功
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	自動変換開始
//---------------------------------------------------------

int whiz_auto_convert(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_auto_convert !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=(unsigned char)-1;		// 失敗
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	拡張プロトコルの問い合わせ
//---------------------------------------------------------

int whiz_query_extensions(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_query_extensions !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=(unsigned char)-1;		// 失敗
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	アプリケーション名登録
//---------------------------------------------------------

int whiz_set_app_name(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_set_app_name !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+8));)	// コンテキスト
	debug(dmsg(10, "APP Name '%s'\n", r->req+10);)	// アプリケーション名

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	グループ名を通知
//---------------------------------------------------------

int whiz_notice_group_name(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_notice_group_name !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+8));)	// コンテキスト
	debug(dmsg(10, "Group Name '%s'\n", r->req+10);)	// グループ名

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	サーバー終了要求
//---------------------------------------------------------

int whiz_killserver(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_killserver !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_TERMINATE;
}


//---------------------------------------------------------
//	関数リスト
//---------------------------------------------------------

struct reqproc {
	int (*func)(REQUEST *r);
};

reqproc req_vector[] = {
/* 0x00 */	{ whiz_initialize },
/* 0x01 */	{ whiz_error },
/* 0x02 */	{ whiz_finalize },
/* 0x03 */	{ whiz_create_context },
/* 0x04 */	{ whiz_duplicate_context },
/* 0x05 */	{ whiz_close_context },
/* 0x06 */	{ whiz_get_dictionary_list },
/* 0x07 */	{ whiz_get_directory_list },
/* 0x08 */	{ whiz_mount_dictionary },
/* 0x09 */	{ whiz_umount_dictionary },
/* 0x0a */	{ whiz_remount_dictionary },
/* 0x0b */	{ whiz_get_mount_dictionary_list },
/* 0x0c */	{ whiz_query_dictionary },
/* 0x0d */	{ whiz_define_word },
/* 0x0e */	{ whiz_delete_word },
/* 0x0f */	{ whiz_convert },
/* 0x10 */	{ whiz_convert_end },
/* 0x11 */	{ whiz_get_candidacy_list },
/* 0x12 */	{ whiz_get_yomi },
/* 0x13 */	{ whiz_subst_yomi },
/* 0x14 */	{ whiz_store_yomi },
/* 0x15 */	{ whiz_store_range },
/* 0x16 */	{ whiz_get_lastyomi },
/* 0x17 */	{ whiz_flush_yomi },
/* 0x18 */	{ whiz_remove_yomi },
/* 0x19 */	{ whiz_get_simplekanji },
/* 0x1a */	{ whiz_resize_pause },
/* 0x1b */	{ whiz_get_hinshi },
/* 0x1c */	{ whiz_get_lex },
/* 0x1d */	{ whiz_get_status },
/* 0x1e */	{ whiz_set_locale },
/* 0x1f */	{ whiz_auto_convert },
/* 0x20 */	{ whiz_query_extensions },
/* 0x21 */	{ whiz_set_app_name },
/* 0x22 */	{ whiz_notice_group_name },
/* 0x23 */	{ whiz_error },
/* 0x24 */	{ whiz_killserver }
};

//---------------------------------------------------------
//	クライアントからの要求を処理
//---------------------------------------------------------

int request(INFO_CONNECTION *ic)
{
	int m;
	char buff[READ_SIZE];

	// ソケットを読み
	m = read(ic->s, buff, sizeof(buff));

	if (m<=0) {
		debug(dmsg(D_WARN, "Warning: Read request failed %d\n", m);)
		debug(dmsg(D_INFO, "IMEがおちたみたいっ☆\n", m);)
		//close(ic->s);		// クライアントとの接続終了
		//ic->s=-1;
		return WS_ERROR;	// エラー(IMEとの接続が落ちた)
	}

	// 要求型
	REQUEST req;
	req.h=(cannaheader_t*)buff;	// ポインタを代入
	req.req=buff;			// ポインタを代入(上のと同じ)
	req.size=m;
	req.ic=ic;

	debug(dmsg(D_INFO, "Request Type: %x (%d)\n", req.h->type, m));
	if (req.h->type > MAXREALREQUEST) {
		debug(dmsg(D_ERR, "Error: Request error [%d] !!\n", req.h->type));
		smsg(D_ERR, "Error: Request error [%d] !!\n", req.h->type);
		return WS_UNKNOWN;	// 未実装
	}

	// プロトコルのタイプ毎に関数を呼ぶ
	m=(* req_vector[req.h->type].func)(&req);

	// ソケットを書く
	socket_write(req.ic->s, req.h, req.size);

	return m;
}


//---------------------------------------------------------
//	ディスパッチループ
//---------------------------------------------------------

void dispatch()
{
	// WHIZを初期化
	whiz.initialize();

	// 要求があるまで待つ
	fd_set readfds;
	timeval overtime;
	int i, n;
	int max;

	// ループ
	max=1;
	for (;;) {
		// fdsの初期化
		FD_ZERO(&readfds);
		// クライアントと接続されているソケットが検査対象に
		debug(dmsg(D_INFO, "max: %d\n", max);)
		for (i=0; i<=MAXSOCK; i++) {
			if (ic[i].s != -1) FD_SET(ic[i].s, &readfds);
		}

		// メッセージが到着しているソケットは？
		overtime.tv_sec = (long)108;	// select 制限時間の設定 目一杯(108秒)
		overtime.tv_usec = (long)0;
		n = select(FD_SETSIZE, (fd_set*)&readfds, NULL, NULL, &overtime);
		debug(dmsg(D_INFO, "select returns: %d\n", n);)

		if (n<0) {
			// エラー
			if (errno == EBADF) { debug(dmsg(D_INFO, "Some client disconnected !!\n");) }
			debug(dmsg(D_ERR, "Error at select !!\n");)
			return;
		}
		if (!n) continue;

		// リクエスト処理っ！
		for (i=1; i<MAXSOCK; i++) {
			// リクエストがあった？
			if ((ic[i].s != -1) && FD_ISSET(ic[i].s, &readfds)) {
				debug(dmsg(D_INFO, "ready for reading: %d\n", i);)
				n=request(&ic[i]);
				// killserver ?
				if (n == WS_TERMINATE) return;

				if (n == WS_ERROR) {
					// クライアントとの接続終了
					close(ic[i].s);
					FD_CLR(ic[i].s, &readfds);
					ic[i].s=-1;
					max--;	// クライアント数を減らす
				}
			}
		}

		// 接続要求(ic[0])
		if (FD_ISSET(ic[0].s, &readfds)) {
			if (max >= MAXSOCK) {
				smsg(D_WARN, "Warning: Server is too busy.\n");
			} else {
				// 接続
				for (i=1; i<MAXSOCK; i++) if (ic[i].s == -1) break;
				ic[i].s = accept(ic[0].s, NULL, NULL);
				debug(dmsg(D_INFO, "accept new connection: %d\n", ic[i].s);)

				if (ic[i].s == -1) {
					debug(dmsg(D_ERR, "Error at accept !!\n");)
					return;
				}
				FD_SET(ic[i].s, &readfds);

				max++;
			}
		}
	}

	return;
}


//---------------------------------------------------------
//	メイン
//---------------------------------------------------------

int main(int argc, char **argv)
{
	int i;

	/* オプションチェック */
	option=0;
	for (i=1; i<argc; i++) {
#ifdef SYSLOG
		/* ログファイルを初期化する */
		if (!strcmp(argv[i], "-syslog")) {
			option|=OP_SYSLOG;
			openlog("whizserver", LOG_PID, LOG_DAEMON);
		}
#endif

#ifdef USE_INET_SOCKET
		if (!strcmp(argv[i], "-inet")) option!=OP_INET;
#endif
	}

	smsg(D_INFO, "Whiz Server version " WHIZ_VER " (" WHIZ_CODE ")\n");
	smsg(D_INFO, "    (C)2003-2023 Yuichiro Nakada\n\n");

	/* コネクションの準備 */
	if (canna_socket_open()) return -1;

#ifndef DEBUG
	/* サーバを子プロセス(デーモン)として起動する */
	/* エラー出力の切り替え、TTYの切り離し */
	daemonize(WHIZ_PID_PATH);
	smsg(D_INFO, " Started by UID %d\n", getuid());

	/* signalの設定 */
	setup_signal((sighandler_t)sig_terminate);
#endif

	/* ディスパッチループ */
	dispatch();

	return 0;
}
