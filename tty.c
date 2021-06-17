#define _POSIX_C_SOURCE 200112L
#include <sys/select.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
// more headers might be required here, not sure...

#if defined(__linux)
 #include <pty.h>
#elif defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
 #include <util.h>
#elif defined(__FreeBSD__) || defined(__DragonFly__)
 #include <libutil.h>
#else
 #error unsupported system
#endif

#include "common.h"
#include "tty.h"
#include "ctlseqs.h"
void sleep_forever(bool hangup); // nnn where do these decs go...

const char* termname = "xterm-256color"; // todo

static Fd cmdfd;
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
}

// I don't use openbsd so I can't confirm whether these pledge calls are correct.
// but they were taken from st, so, probably
#ifdef __OpenBSD__
static void openbsd_pledge(const char* a, void* b) {
	if (pledge(a, b)==-1)
		die("pledge\n");
}
#else
#define openbsd_pledge(a,b) ;
#endif

void tty_init(void) {
	pid = forkpty(&cmdfd, NULL, NULL, NULL);
	if (pid<0) { // ERROR
		die("forkpty failed: %s\n", strerror(errno));
	} else if (pid==0) { // CHILD
		openbsd_pledge("stdio getpw proc exec", NULL); 
		execsh();
		_exit(0);
	} else { // PARENT
		openbsd_pledge("stdio rpath tty proc", NULL); 
		signal(SIGCHLD, sigchld);
	}
}

// read from child process and process the text
size_t tty_read(void) {
	char buf[1024*100];
	ssize_t len = read(cmdfd, buf, sizeof(buf));
	//print("read %ld bytes\n", len);
	if (len>0) {
		// todo: move this stuff into x.c maybe so we don't need buffer.h in this file?
		process_chars(len, buf);
		return len;
	} else if (len<0) {
		print("couldn't read from shell: %s\n", strerror(errno));
		// this is the normal exit condition.
		sleep_forever(true);
	}
	return 0;
}

// don't use this for anything really long
void tty_printf(const char* format, ...) {
	va_list ap;
	va_start(ap, format);
	static char buf[1024];
	int len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	tty_write(len, buf);
}

static int min(int a, int b) {
	return a<b ? a : b;
}

// send data to child process (i.e. keypresses)
void tty_write(size_t len, const char str[len]) {
	const char* pos = str;
	fd_set write_fds, read_fds;
	size_t lim = 256;
	while (len > 0) {
		// wait for uhhhh
		FD_ZERO(&write_fds);
		FD_ZERO(&read_fds);
		FD_SET(cmdfd, &write_fds);
		FD_SET(cmdfd, &read_fds);
		if (pselect(cmdfd+1, &read_fds, &write_fds, NULL, NULL, NULL) < 0) {
			if (errno==EINTR || errno==EAGAIN)
				continue;
			die("select failed: %s\n", strerror(errno));
		}
		
		if (FD_ISSET(cmdfd, &write_fds)) {
			// Only write the bytes written by ttywrite() or the
			// default of 256. This seems to be a reasonable value
			// for a serial line. Bigger values might clog the I/O.
			ssize_t written = write(cmdfd, pos, min(len, lim));
			if (written < 0) {
				die("write error on tty: %s\n", strerror(errno));
			} else if (written >= len) {
				// finished
				break;
			} else {
				// We weren't able to write out everything.
				// This means the buffer is getting full
				// again. Empty it.
				if (len < lim)
					lim = tty_read();
				len -= written;
				pos += written;
			}
		}
		if (FD_ISSET(cmdfd, &read_fds))
			lim = tty_read();
	}
}

void tty_hangup(void) {
	//signal(SIGCHLD, SIG_DFL);
	kill(pid, SIGHUP);
}

void tty_resize(int w, int h, Px pw, Px ph) {
	if (ioctl(cmdfd, TIOCSWINSZ, &(struct winsize){
		.ws_col = w,
		.ws_row = h,
		.ws_xpixel = pw,
		.ws_ypixel = ph,
	}) < 0)
		print("Couldn't set window size: %s\n", strerror(errno));
}

static int max(int a, int b) {
	if (a>b)
		return a;
	return b;
}

//wait until data is recieved on either cmdfd (the fd used to communicate with the child) OR xfd (notifies when x events are recieved)
// returns true if data was recvd on cmdfd
bool tty_wait(Fd xfd, int timeout) {
	fd_set rfd;
	while (1) {
		FD_ZERO(&rfd);
		FD_SET(cmdfd, &rfd);
		FD_SET(xfd, &rfd);
		
		struct timespec seltv = {
			.tv_sec = timeout / 1000,
			.tv_nsec = 1000000 * (timeout % 1000),
		};
		struct timespec* tv = timeout>=0 ? &seltv : NULL;
		
		if (pselect(max(xfd, cmdfd)+1, &rfd, NULL, NULL, tv, NULL) < 0) {
			if (!(errno==EINTR || errno==EAGAIN))
				die("select failed: %s\n", strerror(errno));
		} else
			break;
	}
	return FD_ISSET(cmdfd, &rfd);
}
