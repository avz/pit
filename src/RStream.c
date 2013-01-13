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
#include "WStream.h"

#define RSTREAM_DIR_IS_EMPTY -1
#define RSTREAM_NO_MORE_NOT_ACQUIRED_FILES -2
#define RSTREAM_ROOT_DELETED -3

static int RStream__chunkIsCompleted(struct RStream *ws);
static int RStream__openNextChunk(struct RStream *ws);
static void RStream__scheduleUpdateNotification(struct RStream *ws, useconds_t sleepUsec);
static void RStream__removeRootDir(struct RStream *ws);
static int RStream__openNotAcquiredChunk(struct RStream *rs);
static char RStream__rootHasChunks(struct RStream *rs);
static char RStream__writersIsHere(struct RStream *rs);

void RStream_init(struct RStream *rs, const char *rootDir, char persistentMode, char waitRootMode) {
	rs->chunkNumber = 0;
	rs->chunkFd = -1;
	rs->rootDirFd = -1;
	rs->rootDir = rootDir;
	rs->persistentMode = persistentMode;

	do {
		if(rs->rootDirFd == -1)
			rs->rootDirFd = open(rootDir, O_RDONLY | O_DIRECTORY);

		if(rs->rootDirFd == -1) {
			if(waitRootMode && errno == ENOENT) {
				usleep(100000);
				continue;
			}
			error("open('%s')", rootDir);
		} else if(waitRootMode)  {
			/* тут нужно дополнительно проверить появился ли хоть один чанк */
			if(!RStream__rootHasChunks(rs)) {
				RStream__scheduleUpdateNotification(rs, 1000000);
				continue;
			}

			break;
		} else {
			break;
		}
	} while(waitRootMode);

	if(flock(rs->rootDirFd, LOCK_SH | LOCK_NB) == -1)
		error("Unable to lock %s\n", rootDir);

	debug("Start reading from chunk #%lu", rs->chunkNumber + 1);
}

/**
 * Закрыть дескрипторы и записать оффсет текущего чанка в ФС
 * @param rs
 */
void RStream_destroy(struct RStream *rs) {
	while(rs->chunkFd >= 0) {
		int offsetFileFd;
		char offsetStringBuf[64];
		int offsetStringLen;
		off_t offset = lseek(rs->chunkFd, 0, SEEK_CUR);

		if(offset == (off_t)-1) {
			warning("unable to get current chunk position: %s", strerror(errno));
			break;
		}

		if(offset >= ULONG_MAX) {
			warning("offset is too big: %llu", (unsigned long long)offset);
			break;
		}

		offsetFileFd = open(rs->chunkOffsetPath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
		if(offsetFileFd == -1) {
			warning("Unable to open offset file '%s': %s", rs->chunkOffsetPath, strerror(errno));
			break;
		}

		offsetStringLen = snprintf(offsetStringBuf, sizeof(offsetStringBuf), "%lu\n", (unsigned long)offset);

		if(write(offsetFileFd, offsetStringBuf, (size_t)offsetStringLen) == -1) {
			warning("Unable to write to offset file '%s': %s", rs->chunkOffsetPath, strerror(errno));
		}

		close(offsetFileFd);

		close(rs->chunkFd);
		rs->chunkFd = -1;
	}

	if(rs->rootDirFd >= 0) {
		close(rs->rootDirFd);
		rs->rootDirFd = -1;
	}
}

ssize_t RStream_read(struct RStream *rs, char *buf, ssize_t size) {
	ssize_t r;

	if(rs->chunkFd == -1) {
		if(RStream__openNextChunk(rs) < 0) {
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
					if(RStream__openNextChunk(rs) < 0) {
						debug("end of stream detected");
						RStream__removeRootDir(rs);
						return 0; /* end of stream */
					}

					continue;
				}

				RStream__scheduleUpdateNotification(rs, 100000);
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
	char path[PATH_MAX + 64];

	debug("removing root dir: %s", rs->rootDir);

	snprintf(path, sizeof(path), "%s/.writer.lock", rs->rootDir);
	if(unlink(path) == -1) {
		if(errno != ENOENT)
			warning("unable to unlink() write lock-file '%s'", path);
	}

	if(rmdir(rs->rootDir) == -1) {
		if(errno != ENOENT)
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
	if(numFiles == -1) {
		if(errno == ENOENT)
			return RSTREAM_ROOT_DELETED;

		error("unable to fetch directory listing of '%s'", rs->rootDir);
	}

	for(i=0; i<numFiles; i++) {
		if(list[i]->d_name[0] == '.' || strstr(list[i]->d_name, ".chunk") != list[i]->d_name + (strlen(list[i]->d_name) - 6))
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
		 * просто локаем первый байт, потому что flock() изпользовать
		 * нельзя чтобы не смешивать типы локов
		 */
		if(!flockRangeNB(fd, 0, 1, F_WRLCK)) {
			debug("    - locked");

			close(fd);
			fd = -1;
			continue;
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

static char RStream__writersIsHere(struct RStream *rs) {
	char path[PATH_MAX + 64];
	int fd;

	snprintf(path, sizeof(path), "%s/.writer.lock", rs->rootDir);
	fd = open(path, O_RDONLY);
	if(fd == -1)
		return 0;

	if(flock(fd, LOCK_EX | LOCK_NB) == -1) {
		close(fd);
		return errno == EWOULDBLOCK;
	}

	close(fd);

	return 0;
}

static int RStream__openNextChunk(struct RStream *rs) {
	if(rs->chunkFd >= 0) {
		if(unlink(rs->chunkPath) == -1) {
			error("unlink('%s')", rs->chunkPath);
		}

		if(unlink(rs->chunkOffsetPath) == -1) {
			if(errno != ENOENT)
				warning("unable to unlink offset file '%s': %s", rs->chunkOffsetPath, strerror(errno));
		}

		close(rs->chunkFd);
		rs->chunkFd = -1;
	}

	for(;;) {
		rs->chunkFd = RStream__openNotAcquiredChunk(rs);
		if(rs->chunkFd >= 0)
			break;

		if(rs->chunkFd == RSTREAM_ROOT_DELETED)
			break;

		if(rs->chunkFd == RSTREAM_DIR_IS_EMPTY) {
			if(!rs->persistentMode && !RStream__writersIsHere(rs)) {
				/* писателей не осталось */
				break;
			}
		}

		RStream__scheduleUpdateNotification(rs, 100000);
	}

	/* проверяем нет ли информации о уже прочитанных из чанка данных */
	if(rs->chunkFd >= 0) {
		int offsetFileFd;

		snprintf(rs->chunkOffsetPath, sizeof(rs->chunkOffsetPath), "%s.offset", rs->chunkPath);

		offsetFileFd = open(rs->chunkOffsetPath, O_RDONLY);
		if(offsetFileFd >= 0) {
			char buf[64];
			ssize_t bufLen;
			unsigned long offset;

			debug("Found offset-file '%s'", rs->chunkOffsetPath);

			if((bufLen = read(offsetFileFd, buf, sizeof(buf) - 1)) == -1) {
				warning("Error reading offset-file '%s': %s", rs->chunkOffsetPath, strerror(errno));
			} else {
				buf[bufLen] = 0;

				offset = strtoul(buf, NULL, 10);
				if(offset == ULONG_MAX) {
					warning("unable to parse offset from '%s': invalid string '%s'", rs->chunkOffsetPath, buf);
				} else {
					if(lseek(rs->chunkFd, (off_t)offset, SEEK_SET) == (off_t)-1)
						warning("unable to seek to offset %lu on file '%s'", offset, rs->chunkOffsetPath);
				}

				debug("	offset: strtoul('%s') = %lu", buf, offset);
			}

			close(offsetFileFd);
		} else {
			if(errno != ENOENT)
				warning("Error opening offset-file '%s': %s", rs->chunkOffsetPath, strerror(errno));
		}
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
		if(!flockRangeNB(rs->chunkFd, 1, 1, F_WRLCK)) {
			/* файл залочен, значит писатель ещё пишет */
			return 0;
		}

		break;
	}

	return 1;
}

static char RStream__rootHasChunks(struct RStream *rs) {
	DIR *d;
	struct dirent *e;

	debug("Checking '%s' for chunks", rs->rootDir);

	d = opendir(rs->rootDir);
	if(!d)
		error("opendir(%s)", rs->rootDir);

	while((e = readdir(d))) {
		if(e->d_name[0] == '.' || strstr(e->d_name, ".chunk") != e->d_name + (strlen(e->d_name) - 6))
			continue;

		closedir(d);
		return 1;
	}

	closedir(d);

	return 0;
}

static void RStream__scheduleUpdateNotification(struct RStream *rs, useconds_t sleepUsec) {
#ifdef F_NOTIFY
	if(fcntl(rs->rootDirFd, F_NOTIFY, DN_MODIFY | DN_CREATE) == -1)
		error("fcntl('%s', F_NOTIFY, DN_MODIFY | DN_CREATE)", rs->rootDir);
#endif

	usleep(sleepUsec);
}
