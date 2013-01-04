#include <errno.h>
#include <sys/file.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>

#include "common.h"
#include "RStream.h"

static int RStream__chunkIsCompleted(struct RStream *ws);
static int RStream__openNextChunk(struct RStream *ws);
static void RStream__scheduleUpdateNotification(struct RStream *ws);
static void RStream__findFirstChunk(struct RStream *ws);
static void RStream__removeRootDir(struct RStream *ws);

void RStream_init(struct RStream *ws, const char *rootDir) {
	ws->chunkNumber = 0;
	ws->chunkFd = -1;
	ws->rootDir = rootDir;

	ws->rootDirFd = open(rootDir, O_RDONLY | O_DIRECTORY);
	if(ws->rootDirFd == -1)
		error("open('%s')", rootDir);


	if(flock(ws->rootDirFd, LOCK_EX | LOCK_NB) == -1) {
		if(errno == EWOULDBLOCK)
			error("Stream '%s' already has a reader", rootDir);
		else
			error(rootDir);
	}

	RStream__findFirstChunk(ws);

	debug("Start reading from chunk #%lu", ws->chunkNumber + 1);
}

ssize_t RStream_read(struct RStream *ws, char *buf, ssize_t size) {
	ssize_t r;

	if(ws->chunkFd == -1) {
		if(RStream__openNextChunk(ws) == -1) {
			debug("end of stream detected");
			RStream__removeRootDir(ws);
			return 0; /* end of stream */
		}
	}

	while(1) {
		r = read(ws->chunkFd, buf, (size_t)size);
		if(r == -1 || r == 0) {
			if(r == 0 || errno == EAGAIN || errno == EINTR) {
				/*
				 * нечего было читать, значит надо проверить, не закончился ли чанк
				 */
				if(RStream__chunkIsCompleted(ws)) {
					if(RStream__openNextChunk(ws) == -1) {
						debug("end of stream detected");
						RStream__removeRootDir(ws);
						return 0; /* end of stream */
					}

					continue;
				}

				RStream__scheduleUpdateNotification(ws);
				usleep(100000);
				continue;

			} else if(r == -1) {
				error("read('%s')", ws->chunkPath);
			}
		}
		break;
	}

	return r;
}

static void RStream__removeRootDir(struct RStream *ws) {
	char path[PATH_MAX];

	debug("removing root dir: %s", ws->rootDir);

	snprintf(path, sizeof(path), "%s/.writer.lock", ws->rootDir);
	if(unlink(path) == -1)
		warning("unable to unlink() write lock-file '%s'", path);

	if(rmdir(ws->rootDir) == -1)
		error("rmdir('%s')", ws->rootDir);
}

static int RStream__openNextChunk(struct RStream *ws) {
	if(ws->chunkFd >= 0) {
		if(unlink(ws->chunkPath) == -1)
			error("unlink('%s')", ws->chunkPath);

		close(ws->chunkFd);
	}

	ws->chunkNumber++;

	/* @todo path overflow detection */
	snprintf(ws->chunkPath, sizeof(ws->chunkPath), "%s/%010lu.chunk", ws->rootDir, ws->chunkNumber);

	debug("opening next chunk: %s", ws->chunkPath);
	ws->chunkFd = open(ws->chunkPath, O_RDONLY);
	if(ws->chunkFd == -1)
		debug("error opening chunk [%s]: %s", ws->chunkPath, strerror(errno));

//fprintf(stderr, "open(\"%s\")\n", ws->chunkPath);
	return ws->chunkFd;
}

/**
 * Проверяет, ведётся ли запись в текущий чанк
 * @param ws
 * @return 1 or 0
 */
static int RStream__chunkIsCompleted(struct RStream *ws) {
	while(1) {
		if(flock(ws->chunkFd, LOCK_EX | LOCK_NB) == -1) {
			if(errno == EINTR)
				continue;

			if(errno == EWOULDBLOCK) {
				/* файл залочен, значит писатель всё ещё работает над ним */
//				fprintf(stderr, "locked\n");
				return 0;
			}

		}
		break;
	}

	return 1;
}

static void RStream__findFirstChunk(struct RStream *ws) {
	DIR *d;
	struct dirent *e;
	unsigned long min = ULONG_MAX;

	debug("Scanning '%s' for first chunk", ws->rootDir);

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

		if(cur < min)
			min =cur;
	}

	if(min && min != ULONG_MAX)
		ws->chunkNumber = min - 1;

	closedir(d);
}

static void RStream__scheduleUpdateNotification(struct RStream *ws) {
#ifdef F_NOTIFY
	if(fcntl(ws->rootDirFd, F_NOTIFY, DN_MODIFY) == -1)
		error("fcntl('%s', F_NOTIFY, DN_MODIFY)", ws->rootDir);
#endif
}
