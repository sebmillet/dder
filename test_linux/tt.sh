#!/bin/bash

PRG=../src/dder

if [ -x "$PRG" ]; then

	VERSION=`$PRG -version 2>&1 | grep -i "^dder version [0-9.]\+"`
	VERSION=`echo $VERSION | sed 's/^dder *version *//i;s/ .*$//'`

	echo "Executable: $PRG"
	echo "Executable version: $VERSION"

	COUNTPASS=0
	COUNTFAILED=0

	for f in `ls -d t[0-9][0-9]`; do
		cd $f
		./test.sh -batch
		if [ $? -eq 0 ]; then
			COUNTPASS=$(($COUNTPASS + 1))
		else
			COUNTFAILED=$(($COUNTFAILED + 1))
		fi
		cd ..
	done

	echo "------------------------------------------------------------"
	echo "PASSED: $COUNTPASS"
	echo "FAILED: $COUNTFAILED"
	if [ $COUNTFAILED -eq 0 ]; then
		echo "OK"
	else
		echo "** ERRORS ENCOUNTERED **"
	fi

else

	echo "$PRG not available"
	echo "** ERRORS ENCOUNTERED **"
	exit 1

fi

