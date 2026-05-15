#!/bin/bash
#
for file in $1/*.tif
do
    if test -f "$file"
    then
	echo "processing $file..."
	/usr/local/bin/sipi convert "$file" "${file%%.*}.jp2" --format jpx
    fi
done