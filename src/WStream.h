#ifndef WSTREAM_H
#define	WSTREAM_H

#include <sys/types.h>
#include <stdint.h>

/**
 * длина фиксированная, завязана на реализацию
 * генератора идентификатора в WriteableStream_init().
 * просто так менять константу нельзя
 *
 */
#define WSTREAM_IDENT_LENGTH 19

struct WStream {
	const char *rootDir;
	int writerLockFd;

	/**
	 * порядковый номер текущего чанка
	 */
	unsigned long chunkNumber;

	/* информация по текущему чанку */
	int chunkFd;

	/**
	 * текущий размер чанка (по сути смещение от начала файла)
	 */
	ssize_t chunkSize;

	/**
	 * максимальный размер
	 */
	ssize_t chunkMaxSize;

	int needNewChunk;
};

void WStream_init(struct WStream *ws, const char *rootDir, ssize_t chunkSize, char resumeIsAllowed);
void WStream_destroy(struct WStream *ws);
void WStream_needNewChunk(struct WStream *ws);

void WStream_write(struct WStream *ws, const char *buf, ssize_t len);

#endif	/* WSTREAM_H */

