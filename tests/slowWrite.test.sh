#!/bin/sh

# читатель быстрее писателя

root=/tmp/___bufTest

rm -rf "$root"

payloadSize=1000
payload=$(dd if=/dev/urandom bs=$payloadSize count=1)
chunkSize=$((payloadSize / 10))

echo "$payload" | cstream -t $payloadSize | buf -s $chunkSize -w "$root" &
sleep 0.1

readedPayload=$(buf -r "$root")
retCode=$?

if [ "$retCode" != "0" ]; then
	echo 'unable to read stream'
	exit $retCode
fi

if [ "$payload" != "$readedPayload" ]; then
	echo "payload mismatch:"

	echo "$payload" | hexdump -C
	echo "$readedPayload" | hexdump -C

	exit 1
fi
