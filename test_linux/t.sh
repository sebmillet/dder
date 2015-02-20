#!/bin/sh

find -name test.sh | while read f; do
	sed -i 's/-t\b/-text/;s/-vv\b/-veryverbose/;s/-v\b/-verbose/;s/-w\b/-width/' "$f"
	echo "Modifié '$f'"
done
