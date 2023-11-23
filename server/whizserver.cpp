//---------------------------------------------------------
//	Whiz Server (Japanese Input Method Engine)
//
//		���2003-2023 Yuichiro Nakada
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

WHIZ whiz;		// ��̾�����Ѵ����󥸥�
int option;		// ���ץ����


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
//	UNIX�ɥᥤ�����
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

	// /tmp/.iroha_unix �����
	if (mkdir(IR_UNIX_DIR, 0777) == -1 && errno != EEXIST ) {
		smsg(D_ERR, "Can't open %s error No. %d\n", IR_UNIX_DIR, errno);
	}

	// /tmp/.iroha_unix/IROHA:x �����
	strcpy(unsock.sun_path, IR_UNIX_PATH);
	if (port) snprintf(unsock.sun_path, sizeof(unsock.sun_path), "%s:%d", unsock.sun_path, port);

//#ifdef DEBUG
	unlink(unsock.sun_path);
//#endif

	// Unix �����åȤ����
	if ((request = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		smsg(D_WARN, "Warning: UNIX socket for server failed.\n");
	} else {
		if (bind(request, (struct sockaddr *)&unsock, strlen(unsock.sun_path) + 2) == 0) {
			debug(dmsg(D_INFO, "Soket Filename: %s (%d)\n", unsock.sun_path, request);)
			if (listen(request, 5)) {
				smsg(D_WARN, "Warning: Server could not listen.\n");
				close(request);
				request = -1; /* listen ���� */
			}
		} else {
			smsg(D_WARN, "Warning: Server could not bind.\n");
			close(request);
			request = -1; /* bind ���� */
		}
	}
	(void)umask(old_umask);

	return request;
}
#endif /* use_unix_socket */


//---------------------------------------------------------
//	INET�ɥᥤ�����
//---------------------------------------------------------

#ifdef USE_INET_SOCKET
int canna_inetfd;
struct sockaddr_in insock;

int canna_socket_open_inet(int port)
{
	struct servent *s;
	int one = 1, fd, i;

	// Inet �����åȤ����
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		smsg(D_ERR, "Cannot open inet domain socket.\n");
		return -1;
	}

#ifdef SO_REUSEADDR
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)(&one), sizeof(int));
#endif

	// /etc/services����ݡ����ֹ���������
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
//	��³����
//---------------------------------------------------------

int canna_socket_open()
{
	int i;

	// �����
	//for (i=0; i<=MAXSOCK; i++) ic[i].s=0;
	for (i=0; i<=MAXSOCK; i++) ic[i].s=-1;

	// ��³
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
//	��³���Ĥ���
//---------------------------------------------------------

int canna_socket_close()
{
#ifdef USE_UNIX_SOCKET
	// UNIX�ɥᥤ����Ĥ���
	close(canna_unixfd);
	unlink(unsock.sun_path);
#endif

#ifdef USE_INET_SOCKET
	// INET�ɥᥤ����Ĥ���
	close(canna_inetfd);
#endif

	return 0;
}


pid_t child;
char *pid_file_path;
//---------------------------------------------------------
//	�Ƥ�λ������
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
//	�ǡ����ˤʤ��
//---------------------------------------------------------

int daemonize(const char *pid_path)
{
//	pid_t parent;
#ifndef DEBUG
	int fd;
#endif

	pid_file_path = (char*)pid_path;

	// �Ƥ�PID
//	parent = getpid();

#ifndef DEBUG
	// ɸ�������Ϥ��ڤ�Υ��
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
//	�����С��ν�λ����
//---------------------------------------------------------

inline void term_all(const char *msg)
{
	canna_socket_close();
	debug(dmsg(D_INFO, msg);)
}


//---------------------------------------------------------
//	SIGNAL�ˤ�뽪λ
//---------------------------------------------------------

inline void sig_terminate()
{
	term_all("Terminated by signal.\n");
}


//---------------------------------------------------------
//	SIGNAL������
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
//	���
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
#define WS_ERROR	-1	// IME�Ȥ���³�������
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
//	EUC�򤫤�ʤǻȤ���磻�ɥ���饯�����Ѵ�
//---------------------------------------------------------

int convert_wcs(char *p, char *s, int len)
{
	int n, m;

	m=n=0;
	while (m<len) {
		if (*s>=0x20 && *s<=0x7e) {
			// ASCIIʸ�� (0x20��0x7e)
			*p++=0;
			*p++=*s++;
			m++;
		} else if ((unsigned char)*s>=0xa1 && (unsigned char)*s<=0xfe) {
			// ���� (��1,2�Х��ȤȤ� 0xa1��0xfe)
			*p++=*s++;
			*p++=*s++;
			m+=2;
		} else {
			// ���楳���� (0x00��0x1f,0x7f)
			// Ⱦ���Ҳ�̾ (0x8ea1��0x8edf)
			// ������� (0x8fa1a1��0x8ffefe)
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
//	�磻�ɥ���饯������EUC���Ѵ�
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
//	���顼����
//---------------------------------------------------------

int whiz_error(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_error !! >>\n");)
	smsg(D_ERR, "<< whiz_error !! >>\n");
	r->size=0;
	return WS_SUCCESS;
}


//---------------------------------------------------------
//	�����С������
//---------------------------------------------------------

int whiz_initialize(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_initialize !! >>\n" );)

	// �ץ�ȥ���С������Υ����å�
	char *p;
	p=strchr(r->req+8, ':');				// ex "3.3:root"
	*p++=0;
	debug(dmsg(10, "Protocol Version '%s'\n", r->req+8);)	// �С�������ֹ�
	debug(dmsg(10, "User Name '%s'\n", p);)			// �桼��̾
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

	// ���
	LTOL4(0x30001, r->req);			// �����Хޥ��ʡ��С������(2)+����ƥ������ֹ�(2)
	r->size=4;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	�����С���λ����
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
//	����ƥ����Ȥ���
//---------------------------------------------------------

int whiz_create_context(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_create_context !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(2);		// ����ƥ�����
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	����ƥ����Ȥ�ʣ������
//---------------------------------------------------------

int whiz_duplicate_context(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_duplicate_context !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+4));)	// ����ƥ�����

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(3);		// ����ƥ�����
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	����ƥ����Ȥ�������
//---------------------------------------------------------

int whiz_close_context(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_close_context !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+4));)	// ����ƥ�����

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	����ơ��֥����
//---------------------------------------------------------

int whiz_get_dictionary_list(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_dictionary_list !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+4));)	// ����ƥ�����
	debug(dmsg(10, "Buff size %d\n", S2TOS(r->req+6));)	// �Хåե�������

	//r->h->len=LSBMSB16(2);			// err16
	//r->h->err.e16=LSBMSB16(-1);		// ����
	//r->size=6;

	r->h->len=LSBMSB16(12);			// �ǡ���
	r->h->err.e16=LSBMSB16(1);		// �����
	strcpy(r->req+6, "whiz.dic\0");
	r->size=16;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	����ǥ��쥯�ȥ����
//---------------------------------------------------------

int whiz_get_directory_list(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_directory_list !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+4));)	// ����ƥ�����
	debug(dmsg(10, "Buff size %d\n", S2TOS(r->req+6));)	// �Хåե�������

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// ����
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	����ꥹ���ɲ�
//---------------------------------------------------------

int whiz_mount_dictionary(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_mount_dictionary !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+8));)	// ����ƥ�����
	debug(dmsg(10, "Dic Name '%s'\n", r->req+10);)		// ����̾

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	����ꥹ���ɲ�
//---------------------------------------------------------

int whiz_umount_dictionary(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_umount_dictionary !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+8));)	// ����ƥ�����
	debug(dmsg(10, "Dic Name '%s'\n", r->req+10);)		// ����̾

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	����ꥹ���ѹ�
//---------------------------------------------------------

int whiz_remount_dictionary(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_remount_dictionary !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+8));)	// ����ƥ�����
	debug(dmsg(10, "Dic Name '%s'\n", r->req+10);)		// ����̾

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	����ꥹ�Ȱ���
//---------------------------------------------------------

int whiz_get_mount_dictionary_list(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_mount_dictionary_list !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+4));)	// ����ƥ�����
	debug(dmsg(10, "Buff size %d\n", S2TOS(r->req+6));)	// �Хåե�������

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// ����
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	��Ͽ��ǽ������䤤��碌
//---------------------------------------------------------

int whiz_query_dictionary(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_query_dictionary !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=(unsigned char)-1;		// ����
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	ñ����Ͽ
//---------------------------------------------------------

int whiz_define_word(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_define_word !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=(unsigned char)-1;		// ����
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	ñ����
//---------------------------------------------------------

int whiz_delete_word(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_delete_word !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=(unsigned char)-1;		// ����
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	�Ѵ�����
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

	n=whiz.convert(buff);			// ʸ��1 \0 ʸ��2 \0\0
	n=convert_wcs(r->req+6, buff, n);	// ʸ��1 \0\0 ʸ��2 \0\0 \0\0

	r->h->len=LSBMSB16(2+n);		// err16 + n
	r->h->err.e16=LSBMSB16(whiz.seg);	// ʸ���
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
//	�Ѵ���λ
//---------------------------------------------------------

int whiz_convert_end(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_convert_end !! >>\n");)

#ifdef WORDLEARN
	debug(dmsg(D_INFO, "word learning: %d -seg:%d-\n", L4TOL(r->req+8), S2TOS(r->req+6));)
	if (L4TOL(r->req+8)) {	// 0 �ʤ�ؽ����ʤ�
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
//	�����׵�
//---------------------------------------------------------

int whiz_get_candidacy_list(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_candidacy_list !! >>\n");)

	int c, len;
	char buff[4096];
	len=whiz.get_candidacy(S2TOS(r->req+6), buff, c);	// ����\0 ����\0 �ɤ�\0\0 (r->req+6 ������ʸ���ֹ�)
	debug(dmsg(D_INFO, "current seg: %d, count:%d <%s...(len:%d)>\n", S2TOS(r->req+6), c, buff, len);)
	len=convert_wcs(r->req+6, buff, len);			// ����\0\0 ����\0\0 �ɤ�\0\0\0\0

	r->h->len=LSBMSB16(2+len);		// err16 + len
	r->h->err.e16=LSBMSB16(c);		// �����
	r->size=6+len;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	�ɤߤ��ʼ���
//---------------------------------------------------------

int whiz_get_yomi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_yomi !! >>\n");)

	int n, m;
	n=S2TOS(r->req+6);
	m=strlen(whiz.get_read(whiz.p[whiz.seg-n-1]));
	debug(dmsg(10, "Segmnt %d, %s(%d)\n", n, whiz.get_read(whiz.p[whiz.seg-n-1]), m);)

	r->h->len=LSBMSB16(2+m+4);		// err16 + len + 4 (EOS)
	r->h->err.e16=LSBMSB16(m/2);		// �ɤߤ�Ĺ��
	strncpy(r->req+6, whiz.get_read(whiz.p[whiz.seg-n-1]), m);
	r->req[m+6]=0;
	r->req[m+7]=0;
	r->req[m+8]=0;
	r->req[m+9]=0;
	r->size=6+m+4;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	��ư�Ѵ�
//---------------------------------------------------------

int whiz_subst_yomi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_subst_yomi !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// ����
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	�ɤߤ����ѹ�
//---------------------------------------------------------

int whiz_store_yomi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_store_yomi !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// ����
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	������ʬ��Τߤ�ñʸ���Ѵ�
//---------------------------------------------------------

int whiz_store_range(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_store_range !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// ����
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	̤��ʬ�����
//---------------------------------------------------------

int whiz_get_lastyomi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_lastyomi !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// ����
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	�����Ѵ�
//---------------------------------------------------------

int whiz_flush_yomi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_flush_yomi !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// ����
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	�ɤߥХåե����
//---------------------------------------------------------

int whiz_remove_yomi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_remove_yomi !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=(unsigned char)-1;		// ����
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	����������
//---------------------------------------------------------

int whiz_get_simplekanji(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_simplekanji !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// ����
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	���ڤ��ѹ�
//---------------------------------------------------------

int whiz_resize_pause(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_resize_pause !! >>\n");)

	int n;
	char buff[4096];

	short s;
	s=S2TOS(r->req+8);				// �ɤߤ�Ĺ��(-2ʸ��̤�/-1ʸ�῭�Ф�)
	if (s>=0) {
		s*=2;					// ���Ѥǣ�ʸ��
		n=S2TOS(r->req+6);			// ʸ���ֹ�
		n=strlen(whiz.get_read(whiz.p[whiz.seg-n-1]));
		debug(dmsg(10, "len %d<->%d\n", n, s);)
		if (s>=n) s=-1;
		else s=-2;
	}
	n=whiz.resize(S2TOS(r->req+6), s);		// ʸ���ֹ�
	n=whiz.reconvert(buff, n, S2TOS(r->req+6));	// ʸ��1 \0 ʸ��2 \0\0
	//n=convert_wcs(buff_, buff, n);			// ʸ��1 \0\0 ʸ��2 \0\0 \0\0
	n=convert_wcs(r->req+6, buff, n);		// ʸ��1 \0\0 ʸ��2 \0\0 \0\0
	debug(dmsg(10, "len=%d segment=%d\n", n, whiz.seg);)

	r->h->len=LSBMSB16(2+n);		// err16 + n
	r->h->err.e16=LSBMSB16(whiz.seg);	// ʸ���
	r->size=6+n;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	�ʻ����
//---------------------------------------------------------

int whiz_get_hinshi(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_hinshi !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// ����
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	�����Ǿ���
//---------------------------------------------------------

int whiz_get_lex(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_lex !! >>\n");)

	r->h->len=LSBMSB16(2);			// err16
	r->h->err.e16=LSBMSB16(-1);		// ����
	r->size=6;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	���Ͼ���
//---------------------------------------------------------

int whiz_get_status(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_get_status !! >>\n");)

	r->h->len=LSBMSB16(1+28);		// err8 + 28 (stat)
	r->h->err.e8=0;				// OK !!

	struct stat {
		int bunnum;	// ʸ���ֹ�
		int candnum;	// �����ֹ�
		int maxcand;	/* ������ʸ��θ���� */
		int diccand;	/* FIXME: maxcand - �⡼�ɻ���ʬ */
		int ylen;	/* �����ȸ�����ɤߤ��ʤΥХ��ȿ� */
		int klen;	/* �����ȸ���δ�������ΥХ��ȿ� */
		int tlen;	/* �����ȸ���ι���ñ��� */
	};
	stat *s;
	s=(stat*)(r->req+5);

	int cc = S2TOS(r->req+8);		// �����ȸ����ֹ�
	//DIC *d = (DIC*)whiz.get_candidacy(S2TOS(r->req+6), 0, cc);	// ����������
	DIC *d;
	whiz.get_candidacy(S2TOS(r->req+6), 0, cc, &d);	// ����������
	/*char buff_[4096];
	//smsg(10, "<< whiz_get_status !! >>\n%d\n", S2TOS(r->req+6));
	whiz.get_candidacy(S2TOS(r->req+6), buff_, cc);	// ����������
	//smsg(10, "cand %d\n", cc);*/

	s->maxcand = LSBMSB32(cc);		// �����
	s->diccand = LSBMSB32(cc);		// �����
	cc=S2TOS(r->req+6);
	s->bunnum = LSBMSB32(cc);		// ������ʸ���ֹ�
	s->candnum = LSBMSB32(S2TOS(r->req+8));	// �����ȸ����ֹ�
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
//	Locale��������
//---------------------------------------------------------

int whiz_set_locale(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_set_locale !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// ����
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	��ư�Ѵ�����
//---------------------------------------------------------

int whiz_auto_convert(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_auto_convert !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=(unsigned char)-1;		// ����
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	��ĥ�ץ�ȥ�����䤤��碌
//---------------------------------------------------------

int whiz_query_extensions(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_query_extensions !! >>\n");)

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=(unsigned char)-1;		// ����
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	���ץꥱ�������̾��Ͽ
//---------------------------------------------------------

int whiz_set_app_name(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_set_app_name !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+8));)	// ����ƥ�����
	debug(dmsg(10, "APP Name '%s'\n", r->req+10);)	// ���ץꥱ�������̾

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	���롼��̾������
//---------------------------------------------------------

int whiz_notice_group_name(REQUEST *r)
{
	debug(dmsg(10, "<< whiz_notice_group_name !! >>\n");)
	debug(dmsg(10, "Context %d\n", S2TOS(r->req+8));)	// ����ƥ�����
	debug(dmsg(10, "Group Name '%s'\n", r->req+10);)	// ���롼��̾

	r->h->len=LSBMSB16(1);			// err8
	r->h->err.e8=0;				// OK!!
	r->size=5;

	return WS_SUCCESS;
}


//---------------------------------------------------------
//	�����С���λ�׵�
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
//	�ؿ��ꥹ��
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
//	���饤����Ȥ�����׵�����
//---------------------------------------------------------

int request(INFO_CONNECTION *ic)
{
	int m;
	char buff[READ_SIZE];

	// �����åȤ��ɤ�
	m = read(ic->s, buff, sizeof(buff));

	if (m<=0) {
		debug(dmsg(D_WARN, "Warning: Read request failed %d\n", m);)
		debug(dmsg(D_INFO, "IME���������ߤ����á�\n", m);)
		//close(ic->s);		// ���饤����ȤȤ���³��λ
		//ic->s=-1;
		return WS_ERROR;	// ���顼(IME�Ȥ���³�������)
	}

	// �׵᷿
	REQUEST req;
	req.h=(cannaheader_t*)buff;	// �ݥ��󥿤�����
	req.req=buff;			// �ݥ��󥿤�����(��Τ�Ʊ��)
	req.size=m;
	req.ic=ic;

	debug(dmsg(D_INFO, "Request Type: %x (%d)\n", req.h->type, m));
	if (req.h->type > MAXREALREQUEST) {
		debug(dmsg(D_ERR, "Error: Request error [%d] !!\n", req.h->type));
		smsg(D_ERR, "Error: Request error [%d] !!\n", req.h->type);
		return WS_UNKNOWN;	// ̤����
	}

	// �ץ�ȥ���Υ�������˴ؿ���Ƥ�
	m=(* req_vector[req.h->type].func)(&req);

	// �����åȤ��
	socket_write(req.ic->s, req.h, req.size);

	return m;
}


//---------------------------------------------------------
//	�ǥ����ѥå��롼��
//---------------------------------------------------------

void dispatch()
{
	// WHIZ������
	whiz.initialize();

	// �׵᤬����ޤ��Ԥ�
	fd_set readfds;
	timeval overtime;
	int i, n;
	int max;

	// �롼��
	max=1;
	for (;;) {
		// fds�ν����
		FD_ZERO(&readfds);
		// ���饤����Ȥ���³����Ƥ��륽���åȤ������оݤ�
		debug(dmsg(D_INFO, "max: %d\n", max);)
		for (i=0; i<=MAXSOCK; i++) {
			if (ic[i].s != -1) FD_SET(ic[i].s, &readfds);
		}

		// ��å����������夷�Ƥ��륽���åȤϡ�
		overtime.tv_sec = (long)108;	// select ���»��֤����� �ܰ���(108��)
		overtime.tv_usec = (long)0;
		n = select(FD_SETSIZE, (fd_set*)&readfds, NULL, NULL, &overtime);
		debug(dmsg(D_INFO, "select returns: %d\n", n);)

		if (n<0) {
			// ���顼
			if (errno == EBADF) { debug(dmsg(D_INFO, "Some client disconnected !!\n");) }
			debug(dmsg(D_ERR, "Error at select !!\n");)
			return;
		}
		if (!n) continue;

		// �ꥯ�����Ƚ����á�
		for (i=1; i<MAXSOCK; i++) {
			// �ꥯ�����Ȥ����ä���
			if ((ic[i].s != -1) && FD_ISSET(ic[i].s, &readfds)) {
				debug(dmsg(D_INFO, "ready for reading: %d\n", i);)
				n=request(&ic[i]);
				// killserver ?
				if (n == WS_TERMINATE) return;

				if (n == WS_ERROR) {
					// ���饤����ȤȤ���³��λ
					close(ic[i].s);
					FD_CLR(ic[i].s, &readfds);
					ic[i].s=-1;
					max--;	// ���饤����ȿ��򸺤餹
				}
			}
		}

		// ��³�׵�(ic[0])
		if (FD_ISSET(ic[0].s, &readfds)) {
			if (max >= MAXSOCK) {
				smsg(D_WARN, "Warning: Server is too busy.\n");
			} else {
				// ��³
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
//	�ᥤ��
//---------------------------------------------------------

int main(int argc, char **argv)
{
	int i;

	/* ���ץ��������å� */
	option=0;
	for (i=1; i<argc; i++) {
#ifdef SYSLOG
		/* ���ե�������������� */
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

	/* ���ͥ������ν��� */
	if (canna_socket_open()) return -1;

#ifndef DEBUG
	/* �����Ф�ҥץ���(�ǡ����)�Ȥ��Ƶ�ư���� */
	/* ���顼���Ϥ��ڤ��ؤ���TTY���ڤ�Υ�� */
	daemonize(WHIZ_PID_PATH);
	smsg(D_INFO, " Started by UID %d\n", getuid());

	/* signal������ */
	setup_signal((sighandler_t)sig_terminate);
#endif

	/* �ǥ����ѥå��롼�� */
	dispatch();

	return 0;
}
