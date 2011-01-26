#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>

#define CHILD_SCRIPT  0
#define CHILD_CHECKED 1
#define CHILD_LOAD    2
#define CHILD_ANY     -1

struct child {
	unsigned type: 2;
	unsigned dead: 1;
	pid_t pid;
	struct child *next;
	int in, out;
	time_t timeout;
	int exit_status;
	void (*handle)(struct child *, fd_set *);
};

#define fail_fprintf(f, fmt, args...) \
	fprintf(f, "%s failed: " fmt, scriptname , ##args)

#define fail_perror(str) \
	fail_fprintf(stderr, "%s: %s\n", str, strerror(errno))

static const char *scriptname;
static volatile int sigexit;
static sigset_t sigchld_mask;
static struct child *first_child;
static char *loadcmd = "exec dohell 900";
static fd_set inputs;
static struct child script, load;

void handle_checked_child(struct child *child, fd_set *fds);
void handle_script_child(struct child *child, fd_set *fds);
void handle_load_child(struct child *child, fd_set *fds);

int child_initv(struct child *child, int type, char *argv[])
{
	int pipe_in[2];
	int pipe_out[2];
	int err, i;
	pid_t pid;

	if (pipe(pipe_out) < 0)
		return -errno;

	/* Set the CLOEXEC flag so that we do not leak file
	   descriptors in our children. */
	fcntl(pipe_out[0], F_SETFD,
	      fcntl(pipe_out[0], F_GETFD) | FD_CLOEXEC);
	fcntl(pipe_out[1], F_SETFD,
	      fcntl(pipe_out[0], F_GETFD) | FD_CLOEXEC);

	if (type == CHILD_SCRIPT) {
		if (pipe(pipe_in) < 0)
			goto err_close_pipe_out;

		/* Set the CLOEXEC flag so that we do not leak file
		   descriptors in our children. */
		fcntl(pipe_in[0], F_SETFD,
		      fcntl(pipe_out[0], F_GETFD) | FD_CLOEXEC);
		fcntl(pipe_in[1], F_SETFD,
		      fcntl(pipe_out[0], F_GETFD) | FD_CLOEXEC);

	}

	sigprocmask(SIG_BLOCK, &sigchld_mask, NULL);
	pid = vfork();
	if (pid < 0) {
		sigprocmask(SIG_UNBLOCK, &sigchld_mask, NULL);
		err = -errno;
		goto out_close_pipe;
	}

	if (pid == 0) {
		sigprocmask(SIG_UNBLOCK, &sigchld_mask, NULL);

		switch(type) {
		case CHILD_CHECKED:
		case CHILD_LOAD:
			if (dup2(pipe_out[1], STDOUT_FILENO) < 0) {
				fail_perror("dup2(pipe_out)");
				_exit(EXIT_FAILURE);
			}
			if (dup2(pipe_out[1], STDERR_FILENO) < 0) {
				fail_perror("dup2(pipe_err)");
				_exit(EXIT_FAILURE);
			}
			break;

		case CHILD_SCRIPT:
			if (dup2(pipe_in[0], 1022) < 0) {
				fail_perror("dup2(pipe_in)");
				_exit(EXIT_FAILURE);
			}
			if (dup2(pipe_out[1], 1023) < 0) {
				fail_perror("dup2(pipe_out)");
				_exit(EXIT_FAILURE);
			}
			break;
		}

		err = execvp(argv[0], argv);
		if (err < 0) {
			fail_fprintf(stderr, "execvp(%s): %m", argv[0]);
			_exit(EXIT_FAILURE);
		}
	}
	child->type = type;
	child->dead = 0;
	child->pid = pid;

	child->next = first_child;
	first_child = child;
	sigprocmask(SIG_UNBLOCK, &sigchld_mask, NULL);

	fprintf(stderr, "Started child %d:", pid);
	for (i = 0; argv[i]; i++)
		fprintf(stderr, " %s", argv[i]);
	fputc('\n', stderr);

	close(pipe_out[1]);
	fcntl(pipe_out[0], F_SETFL,
	      fcntl(pipe_out[0], F_GETFL) | O_NONBLOCK);
	child->out = pipe_out[0];
	FD_SET(child->out, &inputs);

	if (type == CHILD_SCRIPT) {
		close(pipe_in[0]);
		child->in = pipe_in[1];
	}

	time(&child->timeout);
	child->timeout += 300;

	switch(type) {
	case CHILD_CHECKED:
		child->handle = handle_checked_child;
		break;
	case CHILD_SCRIPT:
		child->handle = handle_script_child;
		break;
	case CHILD_LOAD:
		child->handle = handle_load_child;
		break;
	}

	return 0;

  out_close_pipe:
	if (type == CHILD_SCRIPT) {
		close(pipe_in[0]);
		close(pipe_in[1]);
	}
  err_close_pipe_out:
	close(pipe_out[0]);
	close(pipe_out[1]);
	return err;
}

int child_init(struct child *child, int type, char *cmdline)
{
	char *argv[] = {
		getenv("SHELL") ?: "sh" , "-c", cmdline, NULL
	};
	return child_initv(child, type, argv);
}

void child_cleanup(struct child *child)
{
	struct child *prev;

	if (child == first_child)
		first_child = child->next;
	else
		for (prev = first_child; prev; prev = prev->next)
			if (prev->next == child) {
				prev->next = child->next;
				break;
			}

	FD_CLR(child->out, &inputs);
	close(child->out);
	if (child->type == CHILD_SCRIPT)
		close(child->in);
}

struct child *child_search(pid_t pid)
{
	struct child *child;

	for (child = first_child; child; child = child->next)
		if (child->pid == pid)
			break;

	return child;
}

int children_done_p(int type)
{
	struct child *child;

	for (child = first_child; child; child = child->next)
		if ((type == CHILD_ANY || type == child->type) && !child->dead)
			return 0;

	return 1;
}

int children_term(int type)
{
	struct child *child;
	struct timespec ts;

	for (child = first_child; child; child = child->next)
		if (type == CHILD_ANY || child->type == type)
			kill(child->pid, SIGTERM);

	ts.tv_sec = 5;
	ts.tv_nsec = 0;
	do {
		if (children_done_p(type))
			return 1;
	} while (nanosleep(&ts, &ts) == -1 && errno == EINTR);

	for (child = first_child; child; child = child->next)
		if ((type == CHILD_ANY || child->type == type)
		    && !child->dead) {
			fail_fprintf(stderr, "child %d would not die, sending "
				   "SIGKILL\n", child->pid);
			kill(child->pid, SIGKILL);
		}

	return 0;
}

void sigchld_handler(int sig)
{
	struct child *child;
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		child = child_search(pid);
		if (!child) {
			fail_fprintf(stderr, "dead child %d not found!\n", pid);
			exit(EXIT_FAILURE);
		}

		child->exit_status = status;
		child->dead = 1;
	}
}

void cleanup(void)
{
	if (!children_term(CHILD_ANY))
		_exit(EXIT_FAILURE);
}

void termsig(int sig)
{
	sigexit = sig;
	cleanup();
}

void copy(int from, int to)
{
	char buffer[4096];
	ssize_t sz;

	do {
		ssize_t written, wsz;
		sz = read(from, buffer, sizeof(buffer));
		if (sz == -1) {
			if (errno == EAGAIN)
				break;
			fail_perror("read");
			exit(EXIT_FAILURE);
		}

		for (written = 0; written < sz;
		     written += (wsz > 0 ? wsz : 0)) {
			wsz = write(to, buffer + written, sz - written);
			if (wsz == -1) {
				fail_perror("write");
				exit(EXIT_FAILURE);
			}
		}
	} while (sz > 0);
}

void handle_checked_child(struct child *child, fd_set *fds)
{
	time_t now = time(NULL);

	if (FD_ISSET(child->out, fds)) {
		copy(child->out, STDOUT_FILENO);
		child->timeout = now + 300;
	}

	if (child->dead) {
		int status = child->exit_status;

		/* A checked child died, this may be abnormal if no
		   termination signal was sent. */
		if (WIFEXITED(status)) {
			if (sigexit)
				goto cleanup;
			fail_fprintf(stderr,
				   "child %d exited with status %d\n",
				   child->pid, WEXITSTATUS(status));
		}

		if (WIFSIGNALED(status)) {
			if (sigexit && WTERMSIG(status) == SIGTERM) {
			  cleanup:
				child_cleanup(child);
				free(child);
				return;
			}
			fail_fprintf(stderr, "child %d exited with signal %d\n",
				   child->pid, WTERMSIG(status));
			if (WCOREDUMP(status))
				fprintf(stderr, "(core dumped)\n");
		}

		exit(EXIT_FAILURE);
		return;
	}

	if (now > child->timeout) {
		fail_fprintf(stderr, "child %d produced no output for 5 minutes.\n",
			   child->pid);
		exit(EXIT_FAILURE);
	}

}

void handle_script_child(struct child *child, fd_set *fds)
{
	static char buffer[4096];
	static unsigned pos;
	char *l, *eol;
	ssize_t sz;
	int rc;

	if (child->dead)
		exit(child->exit_status);

	if (!FD_ISSET(child->out, fds))
		return;

	sz = read(child->out, buffer + pos, sizeof(buffer) - pos);
	for (l = buffer; (eol = strchr(l, '\n')); l = eol + 1) {
		char buf[16];
		*eol = '\0';

		if (!memcmp(l, "check_alive ", 12)) {
			struct child *new_child;

			new_child = malloc(sizeof(*new_child));
			if (!new_child) {
				fail_fprintf(stderr, "allocation failed\n");
				exit(EXIT_FAILURE);
			}

			rc = child_init(new_child, CHILD_CHECKED, l + 12);
			if (rc) {
				fail_perror("child_init");
				exit(EXIT_FAILURE);
			}
		} else if (!memcmp(l, "start_load", 10)) {
			if (!load.dead) {
				fail_fprintf(stderr, "start_load run while load"
					     " script is already running.\n");
				exit(EXIT_FAILURE);
			}

			rc = child_init(&load, CHILD_LOAD, loadcmd);
			if (rc) {
				fail_perror("child_init");
				exit(EXIT_FAILURE);
			}
		} else {
			fprintf(stderr, "Invalid command %s\n", l);
			exit(EXIT_FAILURE);
		}
	}
	if (l != buffer) {
		pos = strlen(l);
		memmove(buffer, l, pos + 1);
	}
}

void handle_load_child(struct child *child, fd_set *fds)
{
	struct child *next;

	if (FD_ISSET(child->out, fds))
		copy(child->out, STDOUT_FILENO);

	if (child->dead) {
		child_cleanup(child);
		if (sigexit)
			return;

		sigexit = SIGTERM;

		fprintf(stderr, "Load script terminated, "
			"terminating checked scripts\n");

		if (!children_term(CHILD_CHECKED))
			exit(EXIT_FAILURE);

		for (child = first_child; child; child = child->next)
			if (child->type == CHILD_CHECKED) {
				child_cleanup(child);
				free(child);
			}

		sigexit = 0;

		write(script.in, "0\n", 2);
		return;
	}
}

void usage(const char *progname)
{
	fprintf(stderr, "%s [-l \"load command\"] script arguments...\n"
		"Run \"script\" with \"arguments\" in a shell supplemented with"
		" a few commands\nsuitable for running Xenomai tests.\n"
		"\"load command\" is a command line to be run in order to"
		" generate load\nwhile running tests.\n", progname);
}

void setpath(void)
{
	char *path;
	size_t path_len;

	path_len = strlen(getenv("PATH") ?: "") + strlen(TESTDIR) + 2;
	path = malloc(path_len);
	if (!path) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	if (getenv("PATH"))
		snprintf(path, path_len, TESTDIR ":%s", getenv("PATH"));
	else
		snprintf(path, path_len, TESTDIR);

	setenv("PATH", path, 1);
}

int main(int argc, char *argv[])
{
	struct sigaction action;
	int rc, maxfd;

	if (argc < 2) {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	if (!strcmp(argv[1], "-h")
	    || !strcmp(argv[1], "--help")) {
		usage(argv[0]);
		exit(EXIT_SUCCESS);
	}

	if (argc >= 3) {
		if (!strcmp(argv[2], "-l")) {
			if (argc == 3) {
				usage(argv[0]);
				exit(EXIT_FAILURE);
			}

			loadcmd = argv[3];

			argv[3] = argv[1];
			argv += 2;
		}
	}
	scriptname = argv[1];

	setpath();

	action.sa_handler = termsig;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_RESTART;
	if (sigaction(SIGTERM, &action, NULL) < 0) {
		fail_perror("sigaction(SIGTERM)");
		exit(EXIT_FAILURE);
	}
	if (sigaction(SIGINT, &action, NULL) < 0) {
		fail_perror("sigaction(SIGTERM)");
		exit(EXIT_FAILURE);
	}

	action.sa_flags |= SA_NOCLDSTOP;
	action.sa_handler = sigchld_handler;
	if (sigaction(SIGCHLD, &action, NULL) < 0) {
		fail_perror("sigaction(SIGCHLD)");
		exit(EXIT_FAILURE);
	}
	atexit(&cleanup);

	load.dead = 1;
	FD_ZERO(&inputs);
	argv[0] = TESTDIR "/xeno-test-run-wrapper";
	rc = child_initv(&script, CHILD_SCRIPT, argv);
	if (rc < 0) {
		fail_fprintf(stderr, "script creation failed: %s\n", strerror(-rc));
		exit(EXIT_FAILURE);
	}
	maxfd = script.out;

	for (;;) {
		struct child *child, *next;
		struct timeval tv;
		fd_set in;
		int rc;

		tv.tv_sec = 0;
		tv.tv_usec = 100000;

		in = inputs;
		rc = select(maxfd + 1, &in, NULL, NULL, &tv);

		if (rc == -1) {
			if (errno == EINTR)
				continue;
			fail_perror("select");
			exit(EXIT_FAILURE);
		}

		maxfd = 0;
		for (child = first_child; child; child = next) {
			next = child->next;

			if (child->out > maxfd)
				maxfd = child->out;

			child->handle(child, &in);
		}

		if (children_done_p(CHILD_ANY)) {
			if (sigexit) {
				signal(sigexit, SIG_DFL);
				raise(sigexit);
			}
			exit(EXIT_SUCCESS);
		}
	}
}
