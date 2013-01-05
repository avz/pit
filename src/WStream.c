#include "WStream.h"
#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <fcntl.h>
#include <glob.h> /* нужно чтобы просто обмануть нетбинс чтобы он думал что size_t задефайнен */
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <limits.h>
#include <dirent.h>

static void WStream__createNextChunk(struct WStream *ws);
static void WStream__acquireWriterLock(struct WStream *ws);
static void WStream__findLastChunk(struct WStream *ws);
static void WStream__write(struct WStream *ws, const char *buf, ssize_t len, char writeInOneChunk);

void WStream_init(struct WStream *ws, const char *rootDir, ssize_t chunkSize, char resumeIsAllowed) {
	int resumeMode = 0;

	ws->rootDir = rootDir;

	ws->chunkFd = -1;
	ws->writerLockFd = -1;
	ws->needNewChunk = 0;
	ws->chunkNumber = 0;
	ws->chunkMaxSize = chunkSize;
	ws->lineBuffer = NULL;
	ws->lineBufferSize = 0;
	ws->lineBufferMaxSize = 0;

	if(mkdir(rootDir, 0755) == -1) {
		if(errno == EEXIST && resumeIsAllowed) {
			resumeMode = 1;
		} else {
			if(errno == EEXIST)
				error("stream directory '%s' already exists. If you want to continue writing try `-c` option", rootDir);
			else
				error("mkdir('%s')", rootDir);
		}
	}

	WStream__acquireWriterLock(ws);

	if(resumeMode) {
		WStream__findLastChunk(ws);
		debug("Start writing to chunk #%lu", ws->chunkNumber + 1);
	}

	WStream__createNextChunk(ws);
}

void WStream_destroy(struct WStream *ws) {
	if(ws->chunkFd >= 0)
		close(ws->chunkFd);

	if(ws->lineBuffer) {
		free(ws->lineBuffer);
		ws->lineBuffer = NULL;
		ws->lineBufferSize = 0;
		ws->lineBufferMaxSize = 0;
	}

	ws->chunkFd = -1;

	if(ws->writerLockFd >= 0)
		close(ws->writerLockFd);

	ws->writerLockFd = -1;
}

void WStream_write(struct WStream *ws, const char *buf, ssize_t len) {
	WStream__write(ws, buf, len, 0);
}

/**
 * В отличии от WStream_write() эта функция гарантирует, что строка будет
 * целиком записана в один чанк без разбивки.
 * Допускается передавать неполные строки. В этом случае функцию бедуте ждать
 * окончания строки перед фактической записью
 *
 * @param ws
 * @param buf
 * @param len
 */
void WStream_writeLines(struct WStream *ws, const char *buf, ssize_t len) {
	char *lastLineEnd;
	ssize_t toWrite;
	ssize_t toBuffer;

	if(!ws->lineBuffer) {
		ws->lineBuffer = malloc(WSTREAM_LINE_MAX_LENGTH);
		ws->lineBufferMaxSize = WSTREAM_LINE_MAX_LENGTH;
		ws->lineBufferSize = 0;
	}

	lastLineEnd = memrchr(buf, '\n', (size_t)len);
	toWrite = lastLineEnd ? (ssize_t)(lastLineEnd - buf) + 1 : 0;
	toBuffer = len - toWrite;

	if(toWrite) {
		if(ws->lineBufferSize) {
			WStream__write(ws, ws->lineBuffer, ws->lineBufferSize, 1);
			ws->lineBufferSize = 0;
		}

		WStream__write(ws, buf, toWrite, 1);

		/* вызываем создание нового чанка, если он нужен */
		WStream__write(ws, NULL, 0, 0);
	}

	if(toBuffer) {
		if(ws->lineBufferSize + toBuffer <= ws->lineBufferMaxSize) {
			memcpy(ws->lineBuffer + ws->lineBufferSize, buf + toWrite, (size_t)toBuffer);
			ws->lineBufferSize += toBuffer;
		} else {
			/*
			 * бида-бида, строка не помещается в буффер
			 * просто флашим весь кусок, не допуская сплита. Пока не встретится
			 * символ '\n' мы будем писать в тот же самый чанк
			 *
			 * Можно обойтись без буферизации и писать сразу в файл, просто запрещая сплит,
			 * но на буферизации мы сэкономим на одном вызове WStream__write() со
			 * сложной логикой и несколько io-вызовов на каждый вызов WStream_writeLines()
			 */
			warning("line is too long, flushing with split supression '\\n'");

			WStream__write(ws, ws->lineBuffer, ws->lineBufferSize, 1);
			ws->lineBufferSize = 0;

			WStream__write(ws, buf + toWrite, toBuffer, 1);
		}
	}

	debug("w: %lu; b: %lu", (long)toWrite, (long)toBuffer);
}

/**
 * @param ws
 * @param buf
 * @param len
 * @param disableSplit Если != 0, то весь буфер будет гарантированно записан в один чанк
 */
static void WStream__write(struct WStream *ws, const char *buf, ssize_t len, char disableSplit) {
	ssize_t written = 0;
	int fd;

	if(!disableSplit && (ws->needNewChunk || ws->chunkSize >= ws->chunkMaxSize)) {
		fd = ws->chunkFd;
		ws->chunkFd = -1;

		WStream__createNextChunk(ws);

		ws->needNewChunk = 0;

		/* сначала создаём новый чанк, а потом уже закрываем старый,
		 * иначе ридер может подумать что запись закончена и убить стрим
		*/
		if(fd >= 0)
			close(fd);
	}

	/* хак, позволяющий сделать проверку на новый чанк, не записывая ничего */
	if(!buf)
		return;

	do {
		while(written != len && (disableSplit || ws->chunkSize < ws->chunkMaxSize)) {
			ssize_t toWriteInThisChunk = len - written;
			ssize_t wr;
			int fd = ws->chunkFd;
			int fdMustBeClosed = 0;

			if(!disableSplit && toWriteInThisChunk >= ws->chunkMaxSize - ws->chunkSize) {
				/*
				 * чанк закончится на этой записи, заранее создаём новый,
				 * чтобы читателю гарантированно было куда переключиться
				 */

				toWriteInThisChunk = ws->chunkMaxSize - ws->chunkSize;

				debug("chunk size overflow (%llu bytes)", (unsigned long long)ws->chunkMaxSize);

				WStream__createNextChunk(ws);
				fdMustBeClosed = 1;
			} else {
				ws->chunkSize += toWriteInThisChunk;
			}

			/* пишем этот кусок с учётом возможных прерываний по сигналам */
			do {
				wr = write(fd, buf + written, (size_t)toWriteInThisChunk);

				if(wr <= 0) {
					if(errno != EINTR)
						error("write(#%d)", fd);
				}

				written += wr;
				toWriteInThisChunk -= wr;
			} while(toWriteInThisChunk);

			if(fdMustBeClosed)
				close(fd);
		}
	} while(written < len);
}

void WStream_needNewChunk(struct WStream *ws) {
	debug("new chunk requested");
	ws->needNewChunk = 1;
}

static void WStream__acquireWriterLock(struct WStream *ws) {
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/.writer.lock", ws->rootDir);

	debug("acquiring writer lock: %s", path);
	ws->writerLockFd = open(path, O_CREAT | O_WRONLY, 0644);
	if(ws->writerLockFd == -1)
		error("unable to open/create lock file '%s'", path);

	if(flock(ws->writerLockFd, LOCK_EX | LOCK_NB) == -1)
		error("unable to acquire writer lock on '%s'. Maybe this stream currenly used", ws->rootDir);
}

static void WStream__createNextChunk(struct WStream *ws) {
	char tmpPathBuf[PATH_MAX + 64];
	char pathBuf[PATH_MAX + 64];

	int fd;

	/*
	 * при переполнении зайдёт на второй круг и при коллизии имени
	 * вывалится с ошибкой
	 */
	ws->chunkNumber++;

	snprintf(
		pathBuf,
		sizeof(pathBuf),
		"%s/%010lu.chunk",
		ws->rootDir,
		ws->chunkNumber
	);

	snprintf(tmpPathBuf, sizeof(tmpPathBuf), "%s.tmp", pathBuf);

	debug("creating new chunk: %s -> %s", tmpPathBuf, pathBuf);

	fd = open(tmpPathBuf, O_CREAT | O_WRONLY | O_EXCL, 0644);
	if(fd < 0)
		error("open('%s')", tmpPathBuf);

	if(flock(fd, LOCK_EX) == -1)
		error("flock('%s', LOCK_EX)", tmpPathBuf);

	if(rename(tmpPathBuf, pathBuf) == -1)
		error("rename('%s', '%s')", tmpPathBuf, pathBuf);

	ws->chunkFd = fd;
	ws->chunkSize = 0;
}

static void WStream__findLastChunk(struct WStream *ws) {
	/* копипаста из RStream__findFirstChunk */
	DIR *d;
	struct dirent *e;
	unsigned long max = 0;

	debug("Scanning '%s' for last chunk", ws->rootDir);

	d = opendir(ws->rootDir);
	if(!d)
		error("opendir(%s)", ws->rootDir);

	while((e = readdir(d))) {
		if(e->d_name[0] == '.')
			continue;

		unsigned long cur = strtoul(e->d_name, NULL, 10);

		debug(" %10lu: %s", cur, e->d_name);

		if(cur == ULONG_MAX || !cur) {
			warning("unable to parse chunk filename: %s", e->d_name);
			continue;
		}

		if(cur > max)
			max = cur;
	}

	if(max && max != ULONG_MAX)
		ws->chunkNumber = max;

	closedir(d);
}
