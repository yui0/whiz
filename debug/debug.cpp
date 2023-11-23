//---------------------------------------------------------
//	Debug for Whiz (Japanese Input Method Engine)
//
//		(C)2003 NAKADA
//---------------------------------------------------------

#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <syslog.h>
#include "engine/whiz.h"


#ifdef DEBUG
//---------------------------------------------------------
//	Print Debug Message
//---------------------------------------------------------

void dmsg(int lev, const char *f, ...)
{
	va_list ap;
	long Time;
	char *date;

	va_start(ap, f);
	char buff[256];
	vsnprintf(buff, 256, f, ap);
	va_end(ap);

	Time = time((long *)0);
	date = (char *)ctime(&Time);
	date[24] = '\0';
	fprintf(stderr, "%s :", date);
	fprintf(stderr, buff);
	fflush(stderr);

	return;
}
#endif


//---------------------------------------------------------
//	Syslog Message
//---------------------------------------------------------

void smsg(int lev, const char *f, ...)
{
	va_list ap;

#ifdef SYSLOG
	va_start(ap, f);
	vsyslog(LOG_ERR, f, ap);
	va_end(ap);
#else
	va_start(ap, f);
	char buff[256];
	vsnprintf(buff, 256, f, ap);
	va_end(ap);
	fprintf(stderr, buff);
	fflush(stderr);
#endif

	return;
}
