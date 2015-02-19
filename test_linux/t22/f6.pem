# Makefile

# Makefile
# idpkcs

# Sébastien Millet, Februay 2015

CPP = gcc
CPPFLAGS = -Wall -Wextra -g -O -Wuninitialized
LINKERFLAGS = -Wall -Wextra -g -O -Wuninitialized
OFLAG = -o

.SUFFIXES : .o .cpp .h
.cpp.o :
	$(CPP) $(CPPFLAGS) -c $<

all : dder

dder : dder.o
	$(CPP) $(LINKERFLAGS) $(OFLAG)dder dder.o

dder.o : dder.c

clean :
	rm *.o
	rm dder

