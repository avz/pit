#!/bin/sh

# читатель быстрее писателя

root=/tmp/___bufTest

rm -rf "$root"

payloadPath="/tmp/payload"
dd if=/dev/urandom bs=$((1024*1024)) count=10 | sort > $payloadPath

if ! ./buf -ls 10000 -w "$root" < $payloadPath; then
	exit $?
fi

./buf -mr "$root" | cstream -t 5000000 > /tmp/1.payload &
./buf -mr "$root" | cstream -t 5000000 > /tmp/2.payload

if [ ! -s /tmp/1.payload  -o ! -s /tmp/2.payload ]; then
	echo " one or all threads read nothing"
	ls -l /tmp/1.payload /tmp/2.payload
	exit 1
fi

poChecksum=$(cat $payloadPath | md5sum)
prChecksum=$(sort /tmp/1.payload /tmp/2.payload | md5sum)

if [ "$poChecksum" != "$prChecksum" ]; then
	echo "Payload mismatch: '$poChecksum' != '$prChecksum'"
	exit 2
fi

#rm "$payloadPath" "/tmp/1.payload" "/tmp/2.payload"
