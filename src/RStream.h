#ifndef RSTREAM_H
#define	RSTREAM_H

#include <limits.h>
#include <inttypes.h>

struct RStream {
	const char *rootDir;
	int rootDirFd;

	char persistentMode;

	/**
	 * порядковый номер текущего чанка
	 */
	unsigned long chunkNumber;

	char chunkPath[PATH_MAX + 64];
	char chunkOffsetPath[PATH_MAX + 64];
	int chunkFd;
};

void RStream_init(struct RStream *ws, const char *rootDir, char persistentMode, char waitRootMode);
void RStream_destroy(struct RStream *ws);
ssize_t RStream_read(struct RStream *ws, char *buf, ssize_t size);

#endif	/* RSTREAM_H */

