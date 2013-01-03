#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include "common.h"
#include "WriteableStream.h"
#include "ReadableStream.h"

#include <signal.h>

unsigned int ALARM_INTERVAL = 1;

struct WriteableStream WRITEABLE_STREAM;

static void _alarmSignalHandler(int sig) {
	WriteableStream_needNewChunk(&WRITEABLE_STREAM);

	alarm(ALARM_INTERVAL);
}

static void writeMode(const char *rootDir, ssize_t chunkSize, unsigned int chunkTimeout) {
	char buf[64 * 1024];
	ssize_t wr;

	debug("Write mode: '%s'. Options:", rootDir);


	if(chunkTimeout) {
		debug("\tchunk timeout: %u", chunkTimeout);
		ALARM_INTERVAL = chunkTimeout;

		signal(SIGALRM, _alarmSignalHandler);
		alarm(ALARM_INTERVAL);
	} else {
		debug("\tchunk timeout: disabled");
	}

	debug("\tchunk size: %llu", (unsigned long long)chunkSize);

	WriteableStream_init(&WRITEABLE_STREAM, rootDir, chunkSize);

	while((wr = read(STDIN_FILENO, buf, sizeof(buf))) > 0)
		WriteableStream_write(&WRITEABLE_STREAM, buf, wr);
}

static void _emptySignalHandler(int sig) {

}

static void readMode(const char *rootDir) {
	char buf[64 * 1024];
	ssize_t rd;

	struct ReadableStream rs;

	debug("Read mode: '%s'", rootDir);

	ReadableStream_init(&rs, rootDir);

	signal(SIGIO, _emptySignalHandler);

	while((rd = ReadableStream_read(&rs, buf, sizeof(buf))) > 0) {
		if(write(STDOUT_FILENO, buf, (size_t)rd) == -1)
			error("write(STDOUT)");
	}
}

static void printUsage(const char *cmd) {
	fprintf(stderr, "Usage: %s { -r | -w [ -s chunkSize ][ -t chunkTimeout ] } /path/to/storage/dir\n", cmd);
}

static void usage(const char *cmd) {
	printUsage(cmd);
	exit(255);
}

int main(int argc, char *argv[]) {
	char writeModeEnabled = 0;
	char readModeEnabled = 0;
	unsigned long chunkSize = ULONG_MAX;
	unsigned long chunkTimeout = ULONG_MAX;

	const char *rootDir = NULL;

	int opt;

	while((opt = getopt(argc, argv, "hwrs:t:")) != -1) {
		switch(opt) {
			case 'w':
				writeModeEnabled = 1;
			break;
			case 'r':
				readModeEnabled = 1;
			break;
			case 's':
				chunkSize = strtoul(optarg, NULL, 10);
				if(chunkSize == ULONG_MAX || chunkSize == 0 || chunkSize >= SSIZE_MAX)
					error("invalid value: %s", optarg);
			break;
			case 't':
				chunkTimeout = strtoul(optarg, NULL, 10);
				if(chunkTimeout == ULONG_MAX || chunkTimeout == 0 || chunkTimeout >= UINT_MAX)
					error("invalid value: %s", optarg);
			break;
			case 'h':
				printUsage(argv[0]);
				exit(0);
			break;
			default:
				usage(argv[0]);
			break;
		}
	}

	if(writeModeEnabled && readModeEnabled)
		usage(argv[0]);

	if(optind >= argc)
		usage(argv[0]);

	if(readModeEnabled && chunkSize != ULONG_MAX)
		usage(argv[0]);

	if(readModeEnabled && chunkTimeout != ULONG_MAX)
		usage(argv[0]);

	/* defaults */

	if(chunkSize == ULONG_MAX)
		chunkSize = 100 * 1024 * 1024;

	if(chunkTimeout == ULONG_MAX)
		chunkTimeout = 0;

	rootDir = argv[optind];

	if(writeModeEnabled)
		writeMode(rootDir, (ssize_t)chunkSize, (unsigned int)chunkTimeout);
	else if(readModeEnabled)
		readMode(rootDir);

	return EXIT_SUCCESS;
}
