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

#define RSTREAM_DIR_IS_EMPTY -1
#define RSTREAM_NO_MORE_NOT_ACQUIRED_FILES -2

static int RStream__chunkIsCompleted(struct RStream *ws);
static int RStream__openNextChunk(struct RStream *ws);
static void RStream__scheduleUpdateNotification(struct RStream *ws);
static void RStream__findFirstChunk(struct RStream *ws);
static void RStream__removeRootDir(struct RStream *ws);
static int RStream__openNotAcquiredChunk(struct RStream *rs);

void RStream_init(struct RStream *rs, const char *rootDir, char multiReaderModeEnabled) {
	int flockOps;
	rs->chunkNumber = 0;
	rs->chunkFd = -1;
	rs->rootDir = rootDir;
	rs->multiReaderMode = multiReaderModeEnabled;

	rs->rootDirFd = open(rootDir, O_RDONLY | O_DIRECTORY);
	if(rs->rootDirFd == -1)
		error("open('%s')", rootDir);

	if(multiReaderModeEnabled)
		flockOps = LOCK_SH;
	else
		flockOps = LOCK_EX;

	if(flock(rs->rootDirFd, flockOps | LOCK_NB) == -1) {
		if(errno == EWOULDBLOCK)
			error("Stream '%s' already has a reader. If you want use multireader mode - add `-m` option", rootDir);
		else
			error(rootDir);
	}

	/* в мультирид-режиме чанк выбирается внутри RStream__openNextChunk */
	if(!rs->multiReaderMode)
		RStream__findFirstChunk(rs);

	debug("Start reading from chunk #%lu", rs->chunkNumber + 1);
}

ssize_t RStream_read(struct RStream *rs, char *buf, ssize_t size) {
	ssize_t r;

	if(rs->chunkFd == -1) {
		if(RStream__openNextChunk(rs) == -1) {
			debug("end of stream detected");
			RStream__removeRootDir(rs);
			return 0; /* end of stream */
		}
	}

	while(1) {
		r = read(rs->chunkFd, buf, (size_t)size);
		if(r == -1 || r == 0) {
			if(r == 0 || errno == EAGAIN || errno == EINTR) {
				/*
				 * нечего было читать, значит надо проверить, не закончился ли чанк
				 */
				if(RStream__chunkIsCompleted(rs)) {
					if(RStream__openNextChunk(rs) == -1) {
						debug("end of stream detected");
						RStream__removeRootDir(rs);
						return 0; /* end of stream */
					}

					continue;
				}

				RStream__scheduleUpdateNotification(rs);
				usleep(100000);
				continue;

			} else if(r == -1) {
				error("read('%s')", rs->chunkPath);
			}
		}
		break;
	}

	return r;
}

static void RStream__removeRootDir(struct RStream *rs) {
	char path[PATH_MAX];

	debug("removing root dir: %s", rs->rootDir);

	snprintf(path, sizeof(path), "%s/.writer.lock", rs->rootDir);
	if(unlink(path) == -1) {
		if(errno != ENOENT || !rs->multiReaderMode)
			warning("unable to unlink() write lock-file '%s'", path);
	}

	if(rmdir(rs->rootDir) == -1) {
		if(errno != ENOENT || !rs->multiReaderMode)
			error("rmdir('%s')", rs->rootDir);
	}
}

/**
 * Сканирует каталог с чанками на предмет первого незахваченного.
 * Используется для выбора чанка в мультирид режиме, когда возможно
 * несколько читателей на поток
 * @param rs
 * @return
 */
static int RStream__openNotAcquiredChunk(struct RStream *rs) {
	int numFiles;
	int i;
	int numChunks = 0;
	int fd = RSTREAM_DIR_IS_EMPTY;
	struct dirent **list;

	debug("staring scandir() on '%s'", rs->rootDir);

	numFiles = scandir(rs->rootDir, &list, NULL, alphasort);
	if(numFiles == -1)
		error("unable to fetch directory listing of '%s'", rs->rootDir);

	for(i=0; i<numFiles; i++) {
		if(list[i]->d_name[0] == '.' || !strstr(list[i]->d_name, ".chunk"))
			continue;

		numChunks++;

		debug("  chunk '%s'", list[i]->d_name);

		/*
		 * тут наша задача - попытаться понять, занят ли этот чанк кем-то.
		 * Из за того, что невозможно атомарно открыть файл и залочить его
		 * приходится открывать и локать отдельно, а после этого проверять
		 * не был лифайл удалён и анлокнут другим читателем в промежутке
		 * между открытием и локом
		 */
		snprintf(rs->chunkPath, sizeof(rs->chunkPath), "%s/%s", rs->rootDir, list[i]->d_name);

		/* обазательно нужно право на запись для lockf() */
		fd = open(rs->chunkPath, O_RDWR);
		if(fd == -1) {
			if(errno == ENOENT) {
				/* ничего страшного, просто файл удалили пока мы сканили */
				debug("    - deleted before lock");

				continue;
			}

			error("opening chunk file '%s'", rs->chunkPath);
		}

		/*
		 * просто локаем пурвый байт, потому что больше локать нечем
		 * т.к. flock() уже используется для синхронизации читателя и писателя
		 */
		if(lockf(fd, F_TLOCK, 1) == -1) {
			if(errno == EACCES || errno == EAGAIN) {
				debug("    - locked");

				close(fd);
				fd = -1;
				continue;
			}

			error("locking chunk '%s'", rs->chunkPath);
		}

		/* проверяем не удалили ли файл до лока */
		if(access(rs->chunkPath, R_OK) == -1) {
			if(errno == ENOENT) {
				debug("    - deleted after lock");

				close(fd);
				fd = -1;
				continue;
			}

			error("checking access on '%s'", rs->chunkPath);
		}

		/* если до сюда дошли, то файл наш */
		break;
	}

	for(i=0; i<numFiles; i++)
		free(list[i]);

	free(list);

	if(!numChunks) {
		debug("no more chunks in '%s'", rs->rootDir);
		return RSTREAM_DIR_IS_EMPTY;
	}

	if(fd < 0)
		return RSTREAM_NO_MORE_NOT_ACQUIRED_FILES;

	return fd;
}

static int RStream__openNextChunk(struct RStream *rs) {
	if(rs->chunkFd >= 0) {
		if(unlink(rs->chunkPath) == -1)
			error("unlink('%s')", rs->chunkPath);

		close(rs->chunkFd);
		rs->chunkFd = -1;
	}

	if(rs->multiReaderMode) {
		do {
			rs->chunkFd = RStream__openNotAcquiredChunk(rs);
			if(rs->chunkFd >= 0)
				break;

			usleep(10000);
		} while(rs->chunkFd == RSTREAM_NO_MORE_NOT_ACQUIRED_FILES);

	} else {
		rs->chunkNumber++;

		/* @todo path overflow detection */
		snprintf(rs->chunkPath, sizeof(rs->chunkPath), "%s/%010lu.chunk", rs->rootDir, rs->chunkNumber);

		debug("opening next chunk: %s", rs->chunkPath);
		rs->chunkFd = open(rs->chunkPath, O_RDONLY);
		if(rs->chunkFd == -1)
			debug("error opening chunk [%s]: %s", rs->chunkPath, strerror(errno));

	}

	return rs->chunkFd;
}

/**
 * Проверяет, ведётся ли запись в текущий чанк
 * @param ws
 * @return 1 or 0
 */
static int RStream__chunkIsCompleted(struct RStream *rs) {
	while(1) {
		if(flock(rs->chunkFd, LOCK_EX | LOCK_NB) == -1) {
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

static void RStream__findFirstChunk(struct RStream *rs) {
	DIR *d;
	struct dirent *e;
	unsigned long min = ULONG_MAX;

	debug("Scanning '%s' for first chunk", rs->rootDir);

	d = opendir(rs->rootDir);
	if(!d)
		error("opendir(%s)", rs->rootDir);

	while((e = readdir(d))) {
		if(e->d_name[0] == '.' || strstr(e->d_name, ".chunk") != e->d_name + (strlen(e->d_name) - 6))
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
		rs->chunkNumber = min - 1;

	closedir(d);
}

static void RStream__scheduleUpdateNotification(struct RStream *rs) {
#ifdef F_NOTIFY
	if(fcntl(rs->rootDirFd, F_NOTIFY, DN_MODIFY) == -1)
		error("fcntl('%s', F_NOTIFY, DN_MODIFY)", rs->rootDir);
#endif
}
