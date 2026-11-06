/*
 * runq: run commands on demand on a phone with no shell. A launchd job (root) watches
 * /var/local/runq: NAME.cmd (one argument per line, the first an absolute path) is renamed
 * NAME.run, run with stdout+stderr in NAME.out, then NAME.rc gets the exit status. Put the .cmd
 * with tools/davinci-files.py (tools/davinci-run.py does the whole round trip).
 *   runq [DIR]
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define MAX_ARGS	64

static void run(const char *dir, const char *name)
{
	char cmd[1024], path[1024], out[1024], rc[1024], buf[8192];
	size_t len = strlen(name) - 4;		/* without ".cmd" */

	snprintf(cmd, sizeof(cmd), "%s/%s", dir, name);
	snprintf(path, sizeof(path), "%s/%.*s.run", dir, (int)len, name);
	snprintf(out, sizeof(out), "%s/%.*s.out", dir, (int)len, name);
	snprintf(rc, sizeof(rc), "%s/%.*s.rc", dir, (int)len, name);
	if (rename(cmd, path) != 0)
		return;
	/* each command in its own child, so a long one (oslog -t) does not hold up the others */
	pid_t child = fork();
	if (child != 0) {
		if (child > 0)
			waitpid(child, NULL, 0);	/* the child forks again at once */
		return;
	}
	if (fork() != 0)
		_exit(0);
	int fd = open(path, O_RDONLY);
	ssize_t n = fd < 0 ? -1 : read(fd, buf, sizeof(buf) - 1);
	if (fd >= 0)
		close(fd);
	if (n <= 0)
		_exit(1);
	buf[n] = 0;

	char *argv[MAX_ARGS + 1];
	int argc = 0;
	for (char *line = strtok(buf, "\n"); line != NULL && argc < MAX_ARGS; line = strtok(NULL, "\n")) {
		size_t l = strlen(line);
		if (l > 0 && line[l - 1] == '\r')
			line[l - 1] = 0;
		argv[argc++] = line;
	}
	argv[argc] = NULL;

	int status = 127;
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, 1, out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	posix_spawn_file_actions_adddup2(&fa, 1, 2);
	posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
	pid_t pid;
	int err = argc > 0 ? posix_spawn(&pid, argv[0], &fa, NULL, argv, environ) : EINVAL;
	posix_spawn_file_actions_destroy(&fa);
	if (err == 0) {
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
			;
		status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
	} else {
		FILE *f = fopen(out, "w");
		if (f != NULL) {
			fprintf(f, "runq: cannot run %s: %s\n", argc > 0 ? argv[0] : "(nothing)", strerror(err));
			fclose(f);
		}
	}
	FILE *f = fopen(rc, "w");
	if (f != NULL) {
		fprintf(f, "%d\n", status);
		fclose(f);
	}
	unlink(path);
	_exit(0);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "/private/var/local/runq";

	mkdir(dir, 0755);
	printf("runq: watching %s\n", dir);
	fflush(stdout);
	for (;;) {
		DIR *d = opendir(dir);
		struct dirent *e;
		while (d != NULL && (e = readdir(d)) != NULL) {
			size_t l = strlen(e->d_name);
			if (l > 4 && strcmp(e->d_name + l - 4, ".cmd") == 0)
				run(dir, e->d_name);
		}
		if (d != NULL)
			closedir(d);
		usleep(500000);
	}
}
