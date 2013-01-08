#!/bin/sh

# один читатель, два писателя

root=/tmp/___bufTest

rm -rf "$root"

payloadPath="/tmp/payload"
dd if=/dev/urandom bs=$((1024*1024)) count=10 | sort > $payloadPath

./buf -Wpmr "$root" | sort > /tmp/1.payload &
j1=$!

if ! dd if="$payloadPath" bs=$((1024*1024)) count=5 | ./buf -ls 10000 -w "$root"; then
	exit $?
fi

if ! dd if="$payloadPath" ibs=$((1024*1024)) skip=5 | ./buf -cls 10000 -w "$root"; then
	exit $?
fi

sleep 1
rm -rf "$root"

wait $j1

poChecksum=$(cat $payloadPath | $MD5)
prChecksum=$(cat /tmp/1.payload | $MD5)

if [ "$poChecksum" != "$prChecksum" ]; then
	echo "Payload mismatch: '$poChecksum' != '$prChecksum'"
	exit 2
fi

rm "$payloadPath" "/tmp/1.payload"
