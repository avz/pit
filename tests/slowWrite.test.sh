#!/bin/sh

# читатель быстрее писателя

root=/tmp/___bufTest

rm -rf "$root"

payloadSize=1000
payload=$(dd if=/dev/urandom bs=$payloadSize count=1)
chunkSize=$((payloadSize / 10))

payloadChecksum=$(echo -n "$payload" | $MD5)

echo -n "$payload" | cstream -t $payloadSize | $CMD -s $chunkSize -w "$root" &

readedPayloadChecksum=$($CMD -Wr "$root" | $MD5)
retCode=$?

if [ "$retCode" != "0" ]; then
	echo 'unable to read stream'
	exit $retCode
fi

if [ "$payloadChecksum" != "$readedPayloadChecksum" ]; then
	echo "payload mismatch: '$payloadChecksum' != '$readedPayloadChecksum'"

	exit 1
fi
