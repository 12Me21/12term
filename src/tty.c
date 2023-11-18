// Interfacing with the pseudoterminal

#define _POSIX_C_SOURCE 200112L

#if defined(__linux)
 #include <pty.h>
#elif defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
 #include <util.h>
#elif defined(__FreeBSD__) || defined(__DragonFly__)
 #define _BSD_SOURCE 1
 #include <libutil.h>
#else
 #error unsupported system
#endif

#include <sys/select.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <sys/ioctl.h>
// more headers might be required here, not sure...

#include "common.h"
#include "tty.h"
#include "ctlseqs.h"
#include "settings.h"

// this is probably most likely always going to be "-c" but just in case..
#define SHELL_EVAL_FLAG "-c"

void sleep_forever(bool hangup); // nnn where do these decs go...

static Fd master_fd;
static pid_t child_pid;

void sigchld(int signum) {
	(void)signum;
	int stat;
	pid_t pid = waitpid(child_pid, &stat, WNOHANG);
	
	if (pid < 0)
		die("waiting for pid %d failed: %s\n", child_pid, strerror(errno));
	
	if (pid != child_pid)
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
	// check, in this order:
	// - SHELL env var
	// - pw_shell
	// - /bin/sh
	char* sh = getenv("SHELL");
	if (sh == NULL)
		sh = pw->pw_shell[0] ? pw->pw_shell : "/bin/sh";
	
	unsetenv("COLUMNS");
	unsetenv("LINES");
	unsetenv("TERMCAP");
	setenv("LOGNAME", pw->pw_name, true);
	setenv("USER", pw->pw_name, true);
	setenv("SHELL", sh, true);
	setenv("HOME", pw->pw_dir, true);
	setenv("TERM", settings.termName, true);
	setenv("TERMCAP", "xterm-256color", true);
	
	signal(SIGCHLD, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGALRM, SIG_DFL);

	char* eval = getenv("LAUNCH_12TERM");

	if (eval == NULL) {
		execvp(sh, (char*[]){sh, NULL});
	} else {
		execvp(sh, (char*[]){sh, SHELL_EVAL_FLAG, eval, NULL});
	}
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
	child_pid = forkpty(&master_fd, NULL, NULL, NULL);
	if (child_pid<0) { // ERROR
		die("forkpty failed: %s\n", strerror(errno));
	} else if (child_pid==0) { // CHILD
		openbsd_pledge("stdio getpw proc exec", NULL); 
		execsh();
		_exit(0);
	} else { // PARENT
		openbsd_pledge("stdio rpath tty proc", NULL); 
		fcntl(master_fd, F_SETFL, O_NONBLOCK);
		signal(SIGCHLD, sigchld);
	}
}

// read from child process and process the text
size_t tty_read(void) {
	char buf[4096]; // how big to make this?
	ssize_t len = read(master_fd, buf, LEN(buf));
	//print("read %ld bytes\n", len);
	if (len>0) {
		process_chars(len, buf);
		return len;
	} else if (len<0 && errno!=EAGAIN) {
		print("couldn't read from shell. status: \"%s\"\n", strerror(errno));
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
	int len = vsnprintf(buf, LEN(buf), format, ap);
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
		FD_SET(master_fd, &write_fds);
		FD_SET(master_fd, &read_fds);
		if (pselect(master_fd+1, &read_fds, &write_fds, NULL, NULL, NULL) < 0) {
			if (errno==EINTR || errno==EAGAIN)
				continue;
			die("select failed: %s\n", strerror(errno));
		}
		
		if (FD_ISSET(master_fd, &write_fds)) {
			// Only write the bytes written by ttywrite() or the
			// default of 256. This seems to be a reasonable value
			// for a serial line. Bigger values might clog the I/O.
			ssize_t written = write(master_fd, pos, min(len, lim));
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
		if (FD_ISSET(master_fd, &read_fds))
			lim = tty_read();
	}
}

void tty_hangup(void) {
	//signal(SIGCHLD, SIG_DFL);
	kill(child_pid, SIGHUP);
}

void tty_resize(int w, int h, Px pw, Px ph) {
	// TIOCSWINSZ = T? IOCtl() Set WINdow SiZe
	if (ioctl(master_fd, TIOCSWINSZ, &(struct winsize){
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

//wait until data is recieved on either master_fd (the fd used to communicate with the child) OR xfd (notifies when x events are recieved)
// returns true if data was recvd on master_fd
bool tty_wait(Fd xfd, Nanosec timeout) {
	fd_set rfd;
	while (1) {
		FD_ZERO(&rfd);
		FD_SET(master_fd, &rfd);
		FD_SET(xfd, &rfd);
		
		struct timespec seltv = {
			.tv_sec = timeout/(1000*1000*1000),
			.tv_nsec = timeout % (1000*1000*1000),
		};
		struct timespec* tv = timeout>=0 ? &seltv : NULL;
		
		if (pselect(max(xfd, master_fd)+1, &rfd, NULL, NULL, tv, NULL) < 0) {
			if (!(errno==EINTR || errno==EAGAIN))
				die("select failed: %s\n", strerror(errno));
		} else
			break;
	}
	return FD_ISSET(master_fd, &rfd);
}
