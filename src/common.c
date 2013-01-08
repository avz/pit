#include "common.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

void _buf_debug(const char *fmt, ...) {
	va_list argp;

	fprintf(stderr, "DBG ");

	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);

	fprintf(stderr, "\n");
}

void warning(const char *fmt, ...) {
	va_list argp;

	fprintf(stderr, "WARN ");

	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);

	fprintf(stderr, "\n");
}

void error(const char *fmt, ...) {
	va_list argp;

	fprintf(stderr, "ERROR! %s. ", strerror(errno));

	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);

	fprintf(stderr, "\n");

	exit((errno) & 0xff ? (errno) & 0xff : 255);
}

uint64_t timemicro() {
	struct timeval tv;
	gettimeofday(&tv, NULL);

	return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}

char flockRangeNB(int fd, off_t start, off_t len, short int type) {
	struct flock l;

	l.l_start = start;
	l.l_len = len;
	l.l_type = type;
	l.l_whence = SEEK_SET;

	if(fcntl(fd, F_SETLK, &l) == -1) {
		if(errno == EAGAIN || errno == EACCES)
			return 0;

		error("unable to lock range");
	}

	return 1;
}
