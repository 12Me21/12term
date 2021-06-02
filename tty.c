#include <limits.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>

#if defined(__linux)
 #include <pty.h>
#elif defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
 #include <util.h>
#elif defined(__FreeBSD__) || defined(__DragonFly__)
 #include <libutil.h>
#endif

#include "debug.h"

static int iofd = 1;
static int cmdfd;
const char* termname = "xterm-24bit";
static pid_t pid;
char *stty_args = "stty raw pass8 nl -echo -iexten -cstopb 38400";

void sigchld(int a) {
	int stat;
	pid_t p = waitpid(pid, &stat, WNOHANG);
	
	if (p < 0)
		die("waiting for pid %hd failed: %s\n", pid, strerror(errno));

	if (pid != p)
		return;

	if (WIFEXITED(stat) && WEXITSTATUS(stat))
		die("child exited with status %d\n", WEXITSTATUS(stat));
	else if (WIFSIGNALED(stat))
		die("child terminated due to signal %d\n", WTERMSIG(stat));
	_exit(0);
}

void stty(char **args) {
	char cmd[_POSIX_ARG_MAX];
	
	size_t n = strlen(stty_args);
	if (n > sizeof(cmd)-1)
		die("incorrect stty parameters\n");
	memcpy(cmd, stty_args, n);
	char* q = cmd + n;
	size_t siz = sizeof(cmd) - n;
	char* s;
	for (char** p = args; p && (s = *p); ++p) {
		n = strlen(s);
		if (n > siz-1)
			die("stty parameter length too long\n");
		*q++ = ' ';
		memcpy(q, s, n);
		q += n;
		siz -= n + 1;
	}
	*q = '\0';
	if (system(cmd) != 0)
		perror("Couldn't call stty");
}

void execsh(char *cmd, char **args) {
	errno = 0;
	const struct passwd* pw = getpwuid(getuid());
	if (pw == NULL) {
		if (errno)
			die("getpwuid: %s\n", strerror(errno));
		else
			die("who are you?\n");
	}
	char* sh = getenv("SHELL");
	if (sh == NULL)
		sh = (pw->pw_shell[0]) ? pw->pw_shell : cmd;
	
	char *prog, *arg;
	if (args) {
		prog = args[0];
		arg = NULL;
	} else {
		prog = sh;
		arg = NULL;
	}
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
	DEFAULT(args, ((char *[]) {prog, arg, NULL}));

	unsetenv("COLUMNS");
	unsetenv("LINES");
	unsetenv("TERMCAP");
	setenv("LOGNAME", pw->pw_name, 1);
	setenv("USER", pw->pw_name, 1);
	setenv("SHELL", sh, 1);
	setenv("HOME", pw->pw_dir, 1);
	setenv("TERM", termname, 1);

	signal(SIGCHLD, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGALRM, SIG_DFL);

	execvp(prog, args);
	_exit(1);
}

int ttynew(const char *line, char *cmd, const char *out, char **args) {
	if (out) {
		//term.mode |= MODE_PRINT;
		iofd = (!strcmp(out, "-")) ? 1 : open(out, O_WRONLY | O_CREAT, 0666);
		if (iofd < 0) {
			print("Error opening %s:%s\n",
				out, strerror(errno));
		}
	}

	if (line) {
		cmdfd = open(line, O_RDWR);
		if (cmdfd < 0)
			die("open line '%s' failed: %s\n", line, strerror(errno));
		dup2(cmdfd, 0);
		stty(args);
		return cmdfd;
	}

	/* seems to work fine on linux, openbsd and freebsd */
	int m, s;
	if (openpty(&m, &s, NULL, NULL, NULL) < 0)
		die("openpty failed: %s\n", strerror(errno));

	switch (pid = fork()) {
	case -1:
		die("fork failed: %s\n", strerror(errno));
		break;
	case 0:
		close(iofd);
		setsid(); /* create a new process group */
		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		if (ioctl(s, TIOCSCTTY, NULL) < 0)
			die("ioctl TIOCSCTTY failed: %s\n", strerror(errno));
		close(s);
		close(m);
#ifdef __OpenBSD__
		if (pledge("stdio getpw proc exec", NULL) == -1)
			die("pledge\n");
#endif
		execsh(cmd, args);
		break;
	default:
#ifdef __OpenBSD__
		if (pledge("stdio rpath tty proc", NULL) == -1)
			die("pledge\n");
#endif
		close(s);
		cmdfd = m;
		signal(SIGCHLD, sigchld);
		break;
	}
	return cmdfd;
}

size_t ttyread(void) {
	static char buf[100];
	static int buflen = 0;
	
	/* append read bytes to unprocessed bytes */
	int ret = read(cmdfd, buf+buflen, sizeof(buf)-buflen);

	switch (ret) {
	case 0:
		exit(0);
		break;
	case -1:
		die("couldn't read from shell: %s\n", strerror(errno));
		break;
	default:
		buflen += ret;
		//int written = twrite(buf, buflen, 0);
		print("read: %*.*s\n", buflen, buflen, buf);
		int written = buflen;
		buflen -= written;
		/* keep any incomplete UTF-8 byte sequence for the next call */
		if (buflen > 0)
			memmove(buf, buf + written, buflen);
	}
	return ret;
}
