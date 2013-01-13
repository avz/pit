#ifndef WSTREAM_H
#define	WSTREAM_H

#include <sys/types.h>
#include <stdint.h>
#include <time.h>

/**
 * длина фиксированная, завязана на реализацию
 * генератора идентификатора в WriteableStream_init().
 * просто так менять константу нельзя
 *
 */
#define WSTREAM_LINE_MAX_LENGTH (1*1024*1024)

struct WStream {
	const char *rootDir;
	int writerLockFd;

	char *lineBuffer;
	ssize_t lineBufferMaxSize;
	ssize_t lineBufferSize;

	char denySignalChunkCreation;

	/*
	 * эти два свойства используются для уникальной идентификации потока
	 */
	unsigned long pid;
	uint32_t startTime;

	uint64_t lastChunkTimemicro;
	/**
	 * порядковый номер текущего чанка внутри таймстемпа
	 */
	unsigned long timestampChunkNumber;

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
};

void WStream_init(struct WStream *ws, const char *rootDir, ssize_t chunkSize);
void WStream_destroy(struct WStream *ws);
void WStream_needNewChunk(struct WStream *ws, char forceCreation);

void WStream_write(struct WStream *ws, const char *buf, ssize_t len);
void WStream_writeLines(struct WStream *ws, const char *buf, ssize_t len);
void WStream_flush(struct WStream *ws);

#endif	/* WSTREAM_H */

