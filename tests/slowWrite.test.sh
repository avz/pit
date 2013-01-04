#!/bin/sh

# читатель быстрее писателя

root=/tmp/___bufTest

rm -rf "$root"

payloadSize=10000
payload=$(dd if=/dev/urandom bs=$payloadSize count=1)

echo "$payload" | cstream -t 5000 | buf -w "$root" &
sleep 0.1

readedPayload=$(buf -r "$root")
retCode=$?

if [ "$retCode" != "0" ]; then
	echo 'unable to read stream'
	exit $retCode
fi

if [ "$payload" != "$readedPayload" ]; then
	echo "payload mismatch"
	exit 1
fi
