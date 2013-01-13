#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include "common.h"
#include "WStream.h"
#include "RStream.h"

#include <signal.h>
#include <errno.h>

unsigned int ALARM_INTERVAL = 1;

struct WStream WSTREAM;
struct RStream RSTREAM;

static void _alarmSignalHandler(int sig) {
	WStream_needNewChunk(&WSTREAM, 0);

	alarm(ALARM_INTERVAL);
}

static void writeMode(const char *rootDir, ssize_t chunkSize, unsigned int chunkTimeout, char binaryMode) {
	char buf[64 * 1024];
	ssize_t wr;
	void (*writerFunc)(struct WStream *, const char *, ssize_t);

	debug("Write mode: '%s'. Options:", rootDir);

	if(chunkTimeout) {
		debug("\tchunk timeout: %u", chunkTimeout);
		ALARM_INTERVAL = chunkTimeout;

		signal(SIGALRM, _alarmSignalHandler);
		alarm(ALARM_INTERVAL);
	} else {
		debug("\tchunk timeout: disabled");
	}

	debug("\tbinary mode: %s", binaryMode ? "enabled" : "disabled");
	debug("\tchunk size: %llu", (unsigned long long)chunkSize);

	WStream_init(&WSTREAM, rootDir, chunkSize);

	if(binaryMode)
		writerFunc = WStream_write;
	else
		writerFunc = WStream_writeLines;

	for(;;) {
		wr = read(STDIN_FILENO, buf, sizeof(buf));

		if(wr <= 0) {
			if(wr == 0) {
				break;
			} else {
				if(errno == EINTR)
					continue;

				error("error reading stdin");
			}
		}

		writerFunc(&WSTREAM, buf, wr);
	}

	WStream_flush(&WSTREAM);
}

static void _emptySignalHandler(int sig) {

}

static void _rstreamDestroySignalHandler(int sig) {
	RStream_destroy(&RSTREAM);

	exit(sig + 128);
}

static void readMode(const char *rootDir, char persistentMode, char waitRootMode) {
	char buf[64 * 1024];
	ssize_t rd;

	debug("Read mode: '%s'. Options:", rootDir);
	debug("\tmilti-reader mode: %s", multiReaderModeEnabled ? "enabled" : "disabled");
	debug("\tpersistent mode: %s", persistentMode ? "enabled" : "disabled");
	debug("\twait root mode: %s", waitRootMode ? "enabled" : "disabled");

	signal(SIGIO, _emptySignalHandler);

	signal(SIGHUP, _rstreamDestroySignalHandler);
	signal(SIGINT, _rstreamDestroySignalHandler);
	signal(SIGTERM, _rstreamDestroySignalHandler);
	signal(SIGPIPE, _rstreamDestroySignalHandler);

	RStream_init(&RSTREAM, rootDir, persistentMode, waitRootMode);

	while((rd = RStream_read(&RSTREAM, buf, sizeof(buf))) > 0) {
		if(write(STDOUT_FILENO, buf, (size_t)rd) == -1)
			error("write(STDOUT)");
	}
}

static void printUsage(const char *cmd) {
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\t%s -r [-pW] /path/to/storage/dir\n", cmd);
	fprintf(stderr, "\t%s -w [ -s chunkSize ][ -t chunkTimeout ][-b] /path/to/storage/dir\n", cmd);
	fprintf(stderr, "Additional info available at https://github.com/avz/buf/\n");
}

static void usage(const char *cmd) {
	printUsage(cmd);
	exit(255);
}

int main(int argc, char *argv[]) {
	char writeModeEnabled = 0;
	char readModeEnabled = 0;
	char binaryMode = 0;
	char persistentMode = 0;
	char waitRootMode = 0;

	unsigned long chunkSize = ULONG_MAX;
	unsigned long chunkTimeout = ULONG_MAX;

	const char *rootDir = NULL;

	int opt;

	while((opt = getopt(argc, argv, "hbwWprs:t:")) != -1) {
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
				if(chunkTimeout == ULONG_MAX || chunkTimeout >= UINT_MAX)
					error("invalid value: %s", optarg);
			break;
			case 'b':
				binaryMode = 1;
			break;
			case 'p':
				persistentMode = 1;
				waitRootMode = 1;
			break;
			case 'W':
				waitRootMode = 1;
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

	if((writeModeEnabled && readModeEnabled) || (!writeModeEnabled && !readModeEnabled))
		usage(argv[0]);

	if(optind >= argc)
		usage(argv[0]);

	if(readModeEnabled && chunkSize != ULONG_MAX)
		usage(argv[0]);

	if(readModeEnabled && chunkTimeout != ULONG_MAX)
		usage(argv[0]);

	if(!writeModeEnabled && binaryMode)
		usage(argv[0]);

	if(!readModeEnabled && persistentMode)
		usage(argv[0]);

	if(!readModeEnabled && waitRootMode)
		usage(argv[0]);

	/* defaults */

	if(chunkSize == ULONG_MAX)
		chunkSize = 1 * 1024 * 1024;

	if(chunkTimeout == ULONG_MAX)
		chunkTimeout = 1;

	rootDir = argv[optind];

	if(writeModeEnabled)
		writeMode(rootDir, (ssize_t)chunkSize, (unsigned int)chunkTimeout, binaryMode);
	else if(readModeEnabled)
		readMode(rootDir, persistentMode, waitRootMode);

	return EXIT_SUCCESS;
}
