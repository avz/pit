#!/bin/sh

# читатель быстрее писателя

root=/tmp/___bufTest

rm -rf "$root"

payload1="0123456789"
payload2="qwertyuiopasdfghjklzxcvbnm"
payload="${payload1}${payload2}" # 36 bytes

chunkSize=10

echo -n "$payload1" | $CMD -s $chunkSize -w "$root"
echo -n "$payload2" | $CMD -s $chunkSize -w "$root"

readedPayload=$($CMD -r "$root")
retCode=$?

if [ "$retCode" != "0" ]; then
	echo 'unable to read stream'
	exit $retCode
fi

if [ "$payload" != "$readedPayload" ]; then
	echo "payload mismatch: '$payload' != '$readedPayload'"

	exit 1
fi
