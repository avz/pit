#!/bin/sh

# пробуем разные размеры чанков

root=/tmp/___bufTest
rm -rf "$root"

fail=0

payload="0123456789qwertyuiopasdfghjklzxcvbnm" # 36 bytes

for size in 1 2 3 7 13 17 18 19 25 30 35 36 37 10000; do
	if ! echo -n "$payload" | ./buf -s "$size" -w "$root"; then
		echo "buf -w error, code $?"
		fail=1
		continue
	fi

	receivedPayload=$(buf -r "$root")
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
