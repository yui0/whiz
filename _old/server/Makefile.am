#INCLUDES = -I$(top_srcdir)
AM_CXXFLAGS = -Wall -Os

sbin_PROGRAMS = whizserver whizkill

whizserver_SOURCES = \
	whizserver.cpp \
	../engine/whiz.h \
	../debug/debug.cpp \
	../debug/debug.h

whizkill_SOURCES = \
	whizkill.c \
	../engine/whiz.h \
	../debug/debug.cpp \
	../debug/debug.h
whizserver_LDADD = ../engine/libwhiz.a
