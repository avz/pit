#!/bin/sh

# читаем из несуществующего потока

root=/tmp/___bufTest

rm -rf "$root"

if echo '' | buf -r "$root"; then
	exit 1
else
	exit 0
fi
