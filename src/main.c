#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include "common.h"
#include "WStream.h"
#include "RStream.h"

#include <signal.h>

unsigned int ALARM_INTERVAL = 1;

struct WStream WSTREAM;
struct RStream RSTREAM;

static void _alarmSignalHandler(int sig) {
	WStream_needNewChunk(&WSTREAM);

	alarm(ALARM_INTERVAL);
}

static void writeMode(const char *rootDir, ssize_t chunkSize, unsigned int chunkTimeout, char resumeModeIsAllowed, char lineModeEnabled) {
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

	debug("\tresume mode: %s", resumeModeIsAllowed ? "enabled" : "disabled");
	debug("\tline mode: %s", lineModeEnabled ? "enabled" : "disabled");
	debug("\tchunk size: %llu", (unsigned long long)chunkSize);

	WStream_init(&WSTREAM, rootDir, chunkSize, resumeModeIsAllowed);

	if(lineModeEnabled) {
		while((wr = read(STDIN_FILENO, buf, sizeof(buf))) > 0)
			WStream_writeLines(&WSTREAM, buf, wr);
	} else {
		while((wr = read(STDIN_FILENO, buf, sizeof(buf))) > 0)
			WStream_write(&WSTREAM, buf, wr);
	}

	WStream_flush(&WSTREAM);
}

static void _emptySignalHandler(int sig) {

}

static void _rstreamDestroySignalHandler(int sig) {
	RStream_destroy(&RSTREAM);

	exit(sig + 128);
}

static void readMode(const char *rootDir, char multiReaderModeEnabled, char persistentMode, char waitRootMode) {
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

	RStream_init(&RSTREAM, rootDir, multiReaderModeEnabled, persistentMode, waitRootMode);

	while((rd = RStream_read(&RSTREAM, buf, sizeof(buf))) > 0) {
		if(write(STDOUT_FILENO, buf, (size_t)rd) == -1)
			error("write(STDOUT)");
	}
}

static void printUsage(const char *cmd) {
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\t%s [-mpW] -r /path/to/storage/dir\n", cmd);
	fprintf(stderr, "\t%s -w [ -s chunkSize ][ -t chunkTimeout ][-cl] /path/to/storage/dir\n", cmd);
	fprintf(stderr, "Additional info available at https://github.com/avz/buf/\n");
}

static void usage(const char *cmd) {
	printUsage(cmd);
	exit(255);
}

int main(int argc, char *argv[]) {
	char writeModeEnabled = 0;
	char readModeEnabled = 0;
	char resumeIsAllowed = 0;
	char lineMode = 0;
	char multiReaderMode = 0;
	char persistentMode = 0;
	char waitRootMode = 0;

	unsigned long chunkSize = ULONG_MAX;
	unsigned long chunkTimeout = ULONG_MAX;

	const char *rootDir = NULL;

	int opt;

	while((opt = getopt(argc, argv, "hmlwWprs:t:c")) != -1) {
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
			case 'c':
				resumeIsAllowed = 1;
			break;
			case 'l':
				lineMode = 1;
			break;
			case 'm':
				multiReaderMode = 1;
			break;
			case 'p':
				persistentMode = 1;
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

	if(!writeModeEnabled && resumeIsAllowed)
		usage(argv[0]);

	if(!writeModeEnabled && lineMode)
		usage(argv[0]);

	if(!readModeEnabled && multiReaderMode)
		usage(argv[0]);

	if(!readModeEnabled && persistentMode)
		usage(argv[0]);

	if(!readModeEnabled && waitRootMode)
		usage(argv[0]);

	/* defaults */

	if(chunkSize == ULONG_MAX)
		chunkSize = 1 * 1024 * 1024;

	if(chunkTimeout == ULONG_MAX)
		chunkTimeout = 0;

	rootDir = argv[optind];

	if(writeModeEnabled)
		writeMode(rootDir, (ssize_t)chunkSize, (unsigned int)chunkTimeout, resumeIsAllowed, lineMode);
	else if(readModeEnabled)
		readMode(rootDir, multiReaderMode, persistentMode, waitRootMode);

	return EXIT_SUCCESS;
}
