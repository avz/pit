#ifndef READABLESTREAM_H
#define	READABLESTREAM_H

#include <limits.h>
#include <inttypes.h>

struct ReadableStream {
	const char *rootDir;
	int rootDirFd;

	/**
	 * порядковый номер текущего чанка
	 */
	unsigned long chunkNumber;

	char chunkPath[PATH_MAX];
	int chunkFd;
};

void ReadableStream_init(struct ReadableStream *ws, const char *rootDir);
ssize_t ReadableStream_read(struct ReadableStream *ws, char *buf, ssize_t size);

#endif	/* READABLESTREAM_H */

