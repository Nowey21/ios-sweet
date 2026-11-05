/*
 * delayrun: run a program once, SECS seconds after boot (davinci has no shell;
 * launchd's RunAtLoad is too early for programs that need the GUI's daemons,
 * StartInterval repeats).
 *   delayrun SECS PROGRAM [ARG...]
 * Waits until kern.boottime + SECS, then execs PROGRAM. Logs to stdout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/time.h>

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: delayrun SECS PROGRAM [ARG...]\n");
		return 2;
	}
	long secs = strtol(argv[1], NULL, 10);
	struct timeval bt = { 0 };
	size_t n = sizeof(bt);
	sysctlbyname("kern.boottime", &bt, &n, NULL, 0);
	struct timeval now;
	gettimeofday(&now, NULL);
	long up = (long)(now.tv_sec - bt.tv_sec);
	if (up < secs)
		sleep((unsigned)(secs - up));
	gettimeofday(&now, NULL);
	printf("delayrun: %ld s since boot, exec %s\n", (long)(now.tv_sec - bt.tv_sec), argv[2]);
	fflush(stdout);
	execv(argv[2], argv + 2);
	perror("delayrun: execv");
	return 1;
}
