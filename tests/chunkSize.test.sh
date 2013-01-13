#!/bin/sh

# пробуем разные размеры чанков

root=/tmp/___bufTest

fail=0

payload="0123456789qwertyuiopasdfghjklzxcvbnm" # 36 bytes
payloadSize=36

for size in 1 2 3 7 13 17 18 19 25 30 35 36 37 10000; do
	rm -rf "$root"

	if ! echo -n "$payload" | $CMD -b -s "$size" -w "$root"; then
		echo "./buf -w error, code $?"
		fail=1
		continue
	fi

	needChunks=$(($payloadSize / $size + 1))
	numChunks=$(ls "$root" | wc -l | awk '{print $1}')

	if [ "$numChunks" != "$needChunks" ]; then
		echo "chunks count mismatch (must be $needChunks, but $numChunks found)"
		ls "$root"
		fail=1
	fi

	receivedPayload=$($CMD -r "$root")
	retCode=$?

	if [ "$retCode" != "0" ]; then
		echo "buf -w error, code $retCode"
		fail=1
		continue
	fi

	if [ "$payload" != "$receivedPayload" ]; then
		fail=1
		echo "chunk size '$size' failed: '$payload' != '$receivedPayload'"
	fi
done

exit "$fail"
