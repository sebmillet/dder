# src/Makefile.am

SUFFIXES = .c .h

AM_CFLAGS = @CFLAGS@ -Wshadow -Wall -I$(srcdir)
AM_LDFLAGS = @LDFLAGS@

bin_PROGRAMS = dder

dder_SOURCES = dder.c
dder_LDFLAGS = 

if WITH_OPENSSL
dder_SOURCES += ppem.h ppem.c
dder_LDFLAGS += -lssl -lcrypto
endif

