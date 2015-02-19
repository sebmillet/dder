#!/bin/sh

PRG=../../src/dder

PRGARGS=$1
N=$2
TNAME=$3
INPUT=$4
OUTPUT=$5
REFERENCE=$6
SUFFIXE1=$7
SUFFIXE2=$8

F=0

REP=$(pwd | sed 's/.*\///')

i=1
while [ $i -le $N ]; do
	II="${INPUT}${i}${SUFFIXE1}"
	OO="${OUTPUT}${i}${SUFFIXE2}"
	RR="${REFERENCE}${i}${SUFFIXE2}"
	$PRG $PRGARGS $II > $OO 2>&1
	if [ "$9" = "-batch" ]; then
		cmp $RR $OO 2>&1 > /dev/null
		if [ "$?" -ne "0" ]; then
			F=1
		fi
	else
		cat $OO
		echo "Should be:"
		cat $RR | sed 's/^/\t/'
		echo ""
		cmp $RR $OO 2>&1 > /dev/null
		if [ "$?" -ne "0" ]; then
			echo "---------- KO ----------"
		fi
	fi
	i=$(($i+1))
done

if [ $F -eq 1 ]; then
	if [ "$9" = "-batch" ]; then
		echo "$REP ** $TNAME: KO"
	fi
	exit 1;
else
	if [ "$9" = "-batch" ]; then
		echo "$REP    $TNAME: OK"
	fi
	exit 0;
fi
