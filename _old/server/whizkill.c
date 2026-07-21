//---------------------------------------------------------
//	Whiz Server (Japanese Input Method Engine)
//
//		(C)2003 NAKADA
//---------------------------------------------------------

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "engine/whiz.h"


//---------------------------------------------------------
//	UNIXドメインでお話する
//---------------------------------------------------------

int initialize_canna(int num)
{
	struct sockaddr_un unaddr;	    /* UNIX socket address. */
	struct sockaddr *addr;		    /* address to connect to */
	int s, len;

	/* いろはサーバと、UNIXドメインで接続 */
	unaddr.sun_family = AF_UNIX;
	if (num) sprintf(unaddr.sun_path, "%s:%d", IR_UNIX_PATH, num);
	else strcpy(unaddr.sun_path, IR_UNIX_PATH);

	addr = (struct sockaddr *)&unaddr;
	len = strlen((char*)unaddr.sun_path) + 2;

	// Open the network connection
	if ((s = socket((int)addr->sa_family, SOCK_STREAM, 0)) >= 0) {
		if (connect(s, addr, len) < 0 ) {
			close(s);
			return -1;
		}
	}

	return s;
}


//---------------------------------------------------------
//	UNIXドメインでのお話をやめる
//---------------------------------------------------------

void terminate_canna(int s)
{
	close(s);

	return;
}


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

#define LOMASK(x)	((x)&255)
#define LTOL4(l, l4)	{ \
	(l4)[0] = LOMASK((l)>>24); (l4)[1] = LOMASK((l)>>16); \
	(l4)[2] = LOMASK((l)>> 8); (l4)[3] = LOMASK((l)); \
}

//---------------------------------------------------------
//	終了シグナルを送る
//---------------------------------------------------------

void communication(int s)
{
	char buff[4];
	LTOL4(0x24000000, buff);
	//socket_write(r->fd, buff, 4);
	write(s, buff, 4);
	socket_read(s, buff, 5);
	return;
}


//---------------------------------------------------------
//	メイン
//---------------------------------------------------------

int main(int argc, char *argv[])
{
	int s;
	s=initialize_canna(0);
	communication(s);
	terminate_canna(s);
	return 0;
}
