# src/Makefile

# dder Makefile for Linux

# Sébastien Millet, Februay 2015

# FORTIFYFLAGS = -Wunreachable-code -Wformat=2 \
#     -D_FORTIFY_SOURCE=2 -fstack-protector --param ssp-buffer-size=4 \
#     -fPIE -pie -Wl,-z,relro,-z,now
CPP = gcc
CPPFLAGS = -O2 -Wall -Wextra -Wuninitialized $(FORTIFYFLAGS)
LINKERFLAGS = -O2 -Wall -Wextra -Wuninitialized $(FORTIFYFLAGS)
OFLAG = -o

.SUFFIXES : .o .c .h
.c.o :
	$(CPP) $(FORTIFYFLAGS) $(CPPFLAGS) -c $<

all : dder

dder : dder.o
	$(CPP) $(LINKERFLAGS) $(OFLAG)$@ $<

dder.o : dder.c

.PHONY: all clean mrproper

mrproper : clean

clean :
	rm -f *.o
	rm -f dder

