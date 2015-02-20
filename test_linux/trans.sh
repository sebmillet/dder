#!/bin/sh

# Uncomment to use it... exit left here to avoid unexpected
# transformation of files.
exit

find -name test.sh | while read f; do
	sed -i 's/-t\b/-text/;s/-vv\b/-veryverbose/;s/-v\b/-verbose/;s/-w\b/-width/' "$f"
	echo "Modifié '$f'"
done
