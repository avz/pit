#ifndef COMMON_H
#define	COMMON_H

#include <sys/types.h>
#include <stdint.h>

void error(const char *fmt, ...);
void warning(const char *fmt, ...);
void _buf_debug(const char *fmt, ...);

#ifdef DEBUG
	#define debug(format, ...) _buf_debug(format, ##__VA_ARGS__)
#else
	#define debug(format, ...)
#endif

#endif	/* COMMON_H */
