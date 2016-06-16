/* $OpenBSD$ */

/*
 * Copyright (c) 2007 Nicholas Marriott <nicholas.marriott@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmux.h"

int		daemon_get_lock(char *);
int		daemon_start(struct event_base *, const char *);

/*
 * Get server create lock. If already held then server start is happening in
 * another client, so block until the lock is released and return -2 to
 * retry. Return -1 on failure to continue and start the server anyway.
 */
int
daemon_get_lock(char *lockfile)
{
	int lockfd;

	log_debug("lock file is %s", lockfile);

	if ((lockfd = open(lockfile, O_WRONLY|O_CREAT, 0600)) == -1) {
		log_debug("open failed: %s", strerror(errno));
		return (-1);
	}

	if (flock(lockfd, LOCK_EX|LOCK_NB) == -1) {
		log_debug("flock failed: %s", strerror(errno));
		if (errno != EAGAIN)
			return (lockfd);
		while (flock(lockfd, LOCK_EX) == -1 && errno == EINTR)
			/* nothing */;
		close(lockfd);
		return (-1);
	}
	log_debug("flock succeeded");

	return (lockfd);
}

/* Connect client to server. */
int
daemon_start(struct event_base *base, const char *path)
{
	int			lockfd = -1;
	char		       *lockfile = NULL;

	xasprintf(&lockfile, "%s.lock", path);
	if ((lockfd = daemon_get_lock(lockfile)) < 0) {
		log_debug("didn't get lock (%d)", lockfd);

		free(lockfile);
		lockfile = NULL;
	}
	log_debug("got lock (%d)", lockfd);

	if (lockfd >= 0 && unlink(path) != 0 && errno != ENOENT) {
		free(lockfile);
		close(lockfd);
		return (-1);
	}
	return server_start(base, lockfd, lockfile, 0);
}

/* Daemon main loop. */
int
daemon_main(struct event_base *base)
{
	/* Ignore SIGCHLD now or daemon() in the server will leave a zombie. */
	signal(SIGCHLD, SIG_IGN);

	clear_signals(0);
	if (event_reinit(base) != 0)
		fatalx("event_reinit failed");

	/* Start the server. */
	return daemon_start(base, socket_path);
}
