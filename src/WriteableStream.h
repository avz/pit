#ifndef WRITEABLESTREAM_H
#define	WRITEABLESTREAM_H

#include <sys/types.h>
#include <stdint.h>

/**
 * длина фиксированная, завязана на реализацию
 * генератора идентификатора в WriteableStream_init().
 * просто так менять константу нельзя
 *
 */
#define WRITEABLE_STREAM_IDENT_LENGTH 19

struct WriteableStream {
	const char *rootDir;

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

void WriteableStream_init(struct WriteableStream *ws, const char *rootDir, ssize_t chunkSize);
void WriteableStream_destroy(struct WriteableStream *ws);
void WriteableStream_needNewChunk(struct WriteableStream *ws);

void WriteableStream_write(struct WriteableStream *ws, const char *buf, ssize_t len);

#endif	/* WRITEABLESTREAM_H */

