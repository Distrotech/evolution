/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *  Radek Doulik <rodo@ximian.com>
 *
 * Copyright 2003 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include <camel/camel-stream-fs.h>

#include "em-spam-filter.h"

static gboolean em_spam_sa_check_spam (CamelMimeMessage *msg);
static void em_spam_sa_report_spam (CamelMimeMessage *msg);
static void em_spam_sa_report_ham (CamelMimeMessage *msg);
static void em_spam_sa_commit_reports (void);

static EMSpamPlugin spam_assassin_plugin =
{
	{
		N_("Spamassassin (built-in)"),
		1,
		em_spam_sa_check_spam,
		em_spam_sa_report_spam,
		em_spam_sa_report_ham,
		em_spam_sa_commit_reports,
	},
	NULL,
	NULL
};

static gboolean em_spam_sa_spamd_tested = FALSE;
static gboolean em_spam_sa_use_spamc = FALSE;
static gint em_spam_sa_spamd_port = -1;

#define d(x) x

static int
pipe_to_sa (CamelMimeMessage *msg, gchar *in, int argc, gchar **argv)
{
	CamelStream *stream;
	int result, status;
	int in_fds[2];
	pid_t pid;
	
	if (argc < 1 || argv[0] == '\0')
		return 0;
	
	if (pipe (in_fds) == -1) {
		/* camel_exception_setv (fms->ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Failed to create pipe to '%s': %s"),
				      argv[0]->value.string, g_strerror (errno)); */
		return -1;
	}
	
	if (!(pid = fork ())) {
		/* child process */
		int maxfd, fd;
		
		fd = open ("/dev/null", O_WRONLY);
		
		if (dup2 (in_fds[0], STDIN_FILENO) < 0 ||
		    dup2 (fd, STDOUT_FILENO) < 0 ||
		    dup2 (fd, STDERR_FILENO) < 0)
			_exit (255);
		
		setsid ();
		
		maxfd = sysconf (_SC_OPEN_MAX);
		if (maxfd > 0) {
			for (fd = 0; fd < maxfd; fd++) {
				if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO)
					close (fd);
			}
		}

		execvp (argv [0], argv);
		
		d(printf ("Could not execute %s: %s\n", argv [0], g_strerror (errno)));
		_exit (255);
	} else if (pid < 0) {
		/* camel_exception_setv (fms->ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Failed to create create child process '%s': %s"),
				      argv[0]->value.string, g_strerror (errno)); */
		return -1;
	}
	
	/* parent process */
	close (in_fds[0]);
	fcntl (in_fds[1], F_SETFL, O_NONBLOCK);
	
	if (msg) {
		stream = camel_stream_fs_new_with_fd (in_fds[1]);
	
		camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (msg), stream);
		camel_stream_flush (stream);
		camel_object_unref (CAMEL_OBJECT (stream));
	} else if (in) {
		write (in_fds [1], in, strlen (in));
		close (in_fds [1]);
	}

	result = waitpid (pid, &status, 0);
	
	if (result == -1 && errno == EINTR) {
		/* child process is hanging... */
		kill (pid, SIGTERM);
		sleep (1);
		result = waitpid (pid, &status, WNOHANG);
		if (result == 0) {
			/* ...still hanging, set phasers to KILL */
			kill (pid, SIGKILL);
			sleep (1);
			result = waitpid (pid, &status, WNOHANG);
		}
	}
	
	if (result != -1 && WIFEXITED (status))
		return WEXITSTATUS (status);
	else
		return -1;
}


#define NPORTS 1

static gboolean
em_spam_sa_test_spamd_running (gint port)
{
	static gchar *sac_args [3] = {
		"/bin/sh",
		"-c",
		NULL
	};
	gboolean retval;

	d(fprintf (stderr, "test if spamd is running (port %d)\n", port);)
	sac_args [2] = port > 0  ? g_strdup_printf ("spamc -x -p %d", port) : g_strdup_printf ("spamc -x");

	retval = pipe_to_sa (NULL, "From test@127.0.0.1", 3, sac_args) == 0;
	g_free (sac_args [2]);

	return retval;
}

static void
em_spam_sa_test_spamd ()
{
	gint i, port = 7830;

	em_spam_sa_use_spamc = FALSE;

	/* if (em_spam_sa_test_spamd_running (-1)) {
		em_spam_sa_use_spamc = TRUE;
		em_spam_sa_spamd_port = -1;
		} else { */
		for (i = 0; i < NPORTS; i ++) {
			if (em_spam_sa_test_spamd_running (port)) {
				em_spam_sa_use_spamc = TRUE;
				em_spam_sa_spamd_port = port;
				break;
			}
			port ++;
		}
		/* } */

	if (!em_spam_sa_use_spamc) {
		static gchar *sad_args [3] = {
			"/bin/sh",
			"-c",
			NULL
		};
		gint i, port = 7830;

		d(fprintf (stderr, "looks like spamd is not running\n");)

		for (i = 0; i < NPORTS; i ++) {
			d(fprintf (stderr, "trying to run spamd at port %d\n", port));

			sad_args [2] = g_strdup_printf ("spamd -p %d --local -d", port);
			if (!pipe_to_sa (NULL, NULL, 3, sad_args)) {
				g_free (sad_args [2]);
				em_spam_sa_use_spamc = TRUE;
				em_spam_sa_spamd_port = port;
				d(fprintf (stderr, "success at port %d\n", port));
				break;
			}
			g_free (sad_args [2]);
			port ++;
		}
	}

	d(fprintf (stderr, "use spamd %d at port %d\n", em_spam_sa_use_spamc, em_spam_sa_spamd_port);)

	em_spam_sa_spamd_tested = TRUE;
}

static gboolean
em_spam_sa_check_spam (CamelMimeMessage *msg)
{
	static gchar *args [3] = {
		"/bin/sh",
		"-c",
		NULL
	};
	gint retval;

	d(fprintf (stderr, "em_spam_sa_check_spam\n"));

	/* FIXME: lock em_spam_sa_spamd_tested to avoid conflicts between threads */
	if (!em_spam_sa_spamd_tested)
		em_spam_sa_test_spamd ();

	args [2] = em_spam_sa_use_spamc
		? (em_spam_sa_spamd_port == -1
		   ? g_strdup ("spamc -c")         /* Exit with a non-zero exit code if the
						      tested message was spam */
		   : g_strdup_printf ("spamc"
				      " -c"        /* Exit with a non-zero exit code if the
						      tested message was spam */
				      " -p %d", em_spam_sa_spamd_port)
)
		: g_strdup ("spamassassin"
			    " --exit-code"         /* Exit with a non-zero exit code if the
						      tested message was spam */
			    " --local");           /* Local tests only (no online tests) */

	retval = pipe_to_sa (msg, NULL, 3, args);

	return retval;
}

static void
em_spam_sa_report_spam (CamelMimeMessage *msg)
{
	static gchar *args [3] = {
		"/bin/sh",
		"-c",
		"sa-learn"
		" --no-rebuild"        /* do not rebuild db */
		" --spam"              /* report spam */
		" --single"            /* single message */
		" --local"             /* local only */
	};

	d(fprintf (stderr, "em_spam_sa_report_spam\n");)

	pipe_to_sa (msg, NULL, 3, args) > 0;
}

static void
em_spam_sa_report_ham (CamelMimeMessage *msg)
{
	static gchar *args [3] = {
		"/bin/sh",
		"-c",
		"sa-learn"
		" --no-rebuild"        /* do not rebuild db */
		" --ham"               /* report ham */
		" --single"            /* single message */
		" --local"             /* local only */
	};

	d(fprintf (stderr, "em_spam_sa_report_ham\n");)

	pipe_to_sa (msg, NULL, 3, args) > 0;
}

static void
em_spam_sa_commit_reports (void)
{
	static gchar *args [3] = {
		"/bin/sh",
		"-c",
		"sa-learn"
		" --rebuild"           /* do not rebuild db */
		" --local"             /* local only */
	};

	d(fprintf (stderr, "em_spam_sa_commit_reports\n");)

	pipe_to_sa (NULL, NULL, 3, args) > 0;
}

const EMSpamPlugin *
em_spam_filter_get_plugin (void)
{
	return &spam_assassin_plugin;
}
