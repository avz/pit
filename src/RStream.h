#ifndef RSTREAM_H
#define	RSTREAM_H

#include <limits.h>
#include <inttypes.h>

struct RStream {
	const char *rootDir;
	int rootDirFd;

	char multiReaderMode;

	/**
	 * порядковый номер текущего чанка
	 */
	unsigned long chunkNumber;

	char chunkPath[PATH_MAX];
	int chunkFd;
};

void RStream_init(struct RStream *ws, const char *rootDir, char multiReaderModeEnabled);
ssize_t RStream_read(struct RStream *ws, char *buf, ssize_t size);

#endif	/* RSTREAM_H */

