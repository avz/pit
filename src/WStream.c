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

static void WStream__createNextChunk(struct WStream *ws);

void WStream_init(struct WStream *ws, const char *rootDir, ssize_t chunkSize) {
	ws->rootDir = rootDir;
	ws->chunkNumber = 0;

	if(mkdir(rootDir, 0755) == -1)
		error("mkdir('%s')", rootDir);

	ws->chunkFd = -1;
	ws->needNewChunk = 0;
	ws->chunkMaxSize = chunkSize;

	WStream__createNextChunk(ws);
}

void WStream_destroy(struct WStream *ws) {
	if(ws->chunkFd)
		close(ws->chunkFd);

	ws->chunkFd = -1;
}

void WStream_write(struct WStream *ws, const char *buf, ssize_t len) {
	ssize_t written = 0;

	if(!ws->chunkNumber)
		WStream__createNextChunk(ws);

	do {
		while(written != len && ws->chunkSize < ws->chunkMaxSize) {
			ssize_t toWriteInThisChunk = len - written;
			ssize_t wr;
			int fd = ws->chunkFd;
			int fdMustBeClosed = 0;

			if(toWriteInThisChunk >= ws->chunkMaxSize - ws->chunkSize) {
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

			if(fdMustBeClosed) {
				close(fd);
			} else if(ws->needNewChunk) {
				WStream__createNextChunk(ws);
				close(fd);

				ws->needNewChunk = 0;
			}
		}
	} while(written < len);

}

void WStream_needNewChunk(struct WStream *ws) {
	debug("new chunk requested");
	ws->needNewChunk = 1;
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
