#include "common.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>

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
