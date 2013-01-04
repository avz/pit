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

void WStream_init(struct WStream *ws, const char *rootDir, ssize_t chunkSize, char resumeIsAllowed) {
	int resumeMode = 0;

	ws->rootDir = rootDir;

	ws->chunkFd = -1;
	ws->writerLockFd = -1;
	ws->needNewChunk = 0;
	ws->chunkNumber = 0;
	ws->chunkMaxSize = chunkSize;

	if(mkdir(rootDir, 0755) == -1) {
		if(errno == EEXIST && resumeIsAllowed) {
			resumeMode = 1;
		} else {
			if(errno = EEXIST)
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

	ws->chunkFd = -1;

	if(ws->writerLockFd >= 0)
		close(ws->writerLockFd);

	ws->writerLockFd = -1;
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
