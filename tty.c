#define _POSIX_C_SOURCE 200112L
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>

#if defined(__linux)
 #include <pty.h>
#elif defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
 #include <util.h>
#elif defined(__FreeBSD__) || defined(__DragonFly__)
 #include <libutil.h>
#else
 #error unsupported system
#endif

#include "debug.h"
#include "ctlseqs.h"
#include "tty.h"
#include "buffer.h"

void sleep_forever(bool hangup);

const char* termname = "xterm-24bit";

static int cmdfd;
static pid_t pid;

void sigchld(int a) {
	int stat;
	pid_t p = waitpid(pid, &stat, WNOHANG);
	
	if (p < 0)
		die("waiting for pid %hd failed: %s\n", pid, strerror(errno));
	
	if (pid != p)
		return;
	
	if (WIFEXITED(stat) && WEXITSTATUS(stat))
		print("child exited with status %d\n", WEXITSTATUS(stat));
	else if (WIFSIGNALED(stat))
		print("child terminated due to signal %d\n", WTERMSIG(stat));	
	
	sleep_forever(false);
}

static void execsh(void) {
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
		sh = pw->pw_shell[0] ? pw->pw_shell : "/bin/sh";
	
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
	
	execvp(sh, (char*[]){sh, NULL});
	_exit(1);
}

int tty_new(void) {
	/* seems to work fine on linux, openbsd and freebsd */
	int m, s;
	if (openpty(&m, &s, NULL, NULL, NULL) < 0)
		die("openpty failed: %s\n", strerror(errno));

	switch (pid = fork()) {
	case -1:
		die("fork failed: %s\n", strerror(errno));
		break;
	case 0:
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
		execsh();
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

size_t tty_read(void) {
	char buf[100];
	
	/* append read bytes to unprocessed bytes */
	int ret = read(cmdfd, buf, sizeof(buf));
	
	switch (ret) {
	case 0:
		sleep_forever(true);
		break;
	case -1:
		print("couldn't read from shell: %s\n", strerror(errno));
		sleep_forever(true);
		break;
	default:
		// todo: move this stuff into x.c maybe so we don't need buffer.h in this file
		process_chars(ret, buf);
		break;
	}
	return ret;
}

void tty_printf(const char* format, ...) {
	va_list ap;
	va_start(ap, format);
	static char buf[1024];
	int len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	tty_write(len, buf);
}

	
void tty_write(size_t n, const char str[n]) {
	const char* s = str;
	fd_set wfd, rfd;
	size_t lim = 256;
	while (n > 0) {
		FD_ZERO(&wfd);
		FD_ZERO(&rfd);
		FD_SET(cmdfd, &wfd);
		FD_SET(cmdfd, &rfd);
		if (pselect(cmdfd+1, &rfd, &wfd, NULL, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue;
			die("select failed: %s\n", strerror(errno));
		}
		if (FD_ISSET(cmdfd, &wfd)) {
			/*
			 * Only write the bytes written by ttywrite() or the
			 * default of 256. This seems to be a reasonable value
			 * for a serial line. Bigger values might clog the I/O.
			 */
			ssize_t written = write(cmdfd, s, (n < lim)? n : lim);
			if (written < 0)
				goto write_error;
			if (written < n) {
				/*
				 * We weren't able to write out everything.
				 * This means the buffer is getting full
				 * again. Empty it.
				 */
				if (n < lim)
					lim = tty_read();
				n -= written;
				s += written;
			} else {
				/* All bytes have been written. */
				break;
			}
		}
		if (FD_ISSET(cmdfd, &rfd))
			lim = tty_read();
	}
	return;

 write_error:
	die("write error on tty: %s\n", strerror(errno));
}

void tty_hangup(void) {
	signal(SIGCHLD, SIG_DFL);
	kill(pid, SIGHUP);
}

// ugly
void tty_resize(int w, int h) {
	if (ioctl(cmdfd, TIOCSWINSZ, &(struct winsize){
				.ws_col = T.width,
				.ws_row = T.height,
				.ws_xpixel = w,
				.ws_ypixel = h,
			}) < 0)
		print("Couldn't set window size: %s\n", strerror(errno));
}

void tty_paste_text(int len, const char text[len]) {
	if (T.bracketed_paste)
		tty_write(6, "\x1B[200~");
	tty_write(len, text);
	if (T.bracketed_paste)
		tty_write(6, "\x1B[201~");
}
