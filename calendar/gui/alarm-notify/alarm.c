/* Evolution calendar - Low-level alarm timer mechanism
 *
 * Copyright (C) 2000 Helix Code, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Authors: Miguel de Icaza <miguel@ximian.com>
 *          Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <gdk/gdk.h>
#include "alarm.h"



/* Whether the timer system has been initialized */
static gboolean alarm_inited;

/* The pipes used to notify about an alarm */
static int alarm_pipes [2];

/* The list of pending alarms */
static GList *alarms;

/* A queued alarm structure */
typedef struct {
	time_t             trigger;
	AlarmFunction      alarm_fn;
	gpointer           data;
	AlarmDestroyNotify destroy_notify_fn;
} AlarmRecord;



/* SIGALRM handler.  Notifies the callback about the alarm. */
static void
alarm_signal (int arg)
{
	char c = 0;

	write (alarm_pipes [1], &c, 1);
}

/* Sets up an itimer and returns a success code */
static gboolean
setup_itimer (time_t diff)
{
	struct itimerval itimer;
	int v;

	itimer.it_interval.tv_sec = 0;
	itimer.it_interval.tv_usec = 0;
	itimer.it_value.tv_sec = diff;
	itimer.it_value.tv_usec = 0;

	v = setitimer (ITIMER_REAL, &itimer, NULL);

	return (v == 0) ? TRUE : FALSE;
}

/* Clears the itimer we have pending */
static gboolean
clear_itimer (void)
{
	return setup_itimer (0);
}

/* Removes the head alarm, returns it, and schedules the next alarm in the
 * queue.
 */
static AlarmRecord *
pop_alarm (void)
{
	AlarmRecord *ar;
	GList *l;

	if (!alarms)
		return NULL;

	ar = alarms->data;

	l = alarms;
	alarms = g_list_remove_link (alarms, l);
	g_list_free_1 (l);

	if (alarms) {
		time_t now;
		AlarmRecord *new_ar;

		now = time (NULL);
		new_ar = alarms->data;

		if (!setup_itimer (new_ar->trigger - now)) {
			g_message ("pop_alarm(): Could not reset the timer!  "
				   "Weird things will happen.");

			/* FIXME: should we free the alarm list?  What
			 * about further alarm removal requests that
			 * will fail?
			 */
		}
	} else
		if (!clear_itimer ())
			g_message ("pop_alarm(): Could not clear the timer!  "
				   "Weird things may happen.");

	return ar;
}

/* Input handler for our own alarm notification pipe */
static void
alarm_ready (gpointer data, gint fd, GdkInputCondition cond)
{
	AlarmRecord *ar;
	char c;

	if (read (alarm_pipes [0], &c, 1) != 1) {
		g_message ("alarm_ready(): Uh?  Could not read from notification pipe.");
		return;
	}

	g_assert (alarms != NULL);
	ar = pop_alarm ();

	g_print ("alarm_ready(): Notifying about alarm on %s\n", ctime (&ar->trigger));

	(* ar->alarm_fn) (ar, ar->trigger, ar->data);

	if (ar->destroy_notify_fn)
		(* ar->destroy_notify_fn) (ar, ar->data);

	g_free (ar);
}

static int
compare_alarm_by_time (gconstpointer a, gconstpointer b)
{
	const AlarmRecord *ara = a;
	const AlarmRecord *arb = b;
	time_t diff;

	diff = ara->trigger - arb->trigger;
	return (diff < 0) ? -1 : (diff > 0) ? 1 : 0;
}

/* Adds an alarm to the queue and sets up the timer */
static gboolean
queue_alarm (time_t now, AlarmRecord *ar)
{
	time_t diff;
	AlarmRecord *old_head;

	if (alarms)
		old_head = alarms->data;
	else
		old_head = NULL;

	alarms = g_list_insert_sorted (alarms, ar, compare_alarm_by_time);

	if (old_head == alarms->data)
		return TRUE;

	/* Set the timer for removal upon activation */

	diff = ar->trigger - now;
	if (!setup_itimer (diff)) {
		GList *l;

		g_message ("queue_alarm(): Could not set up timer!  Not queueing alarm.");

		l = g_list_find (alarms, ar);
		g_assert (l != NULL);

		alarms = g_list_remove_link (alarms, l);
		g_list_free_1 (l);
		return FALSE;
	}

	return TRUE;
}

/**
 * alarm_add:
 * @trigger: Time at which alarm will trigger.
 * @alarm_fn: Callback for trigger.
 * @data: Closure data for callback.
 *
 * Adds an alarm to trigger at the specified time.  The @alarm_fn will be called
 * with the provided data and the alarm will be removed from the trigger list.
 *
 * Return value: An identifier for this alarm; it can be used to remove the
 * alarm later with alarm_remove().  If the trigger time occurs in the past, then
 * the alarm will not be queued and the function will return NULL.
 **/
gpointer
alarm_add (time_t trigger, AlarmFunction alarm_fn, gpointer data,
	   AlarmDestroyNotify destroy_notify_fn)
{
	time_t now;
	AlarmRecord *ar;

	g_return_val_if_fail (alarm_inited, NULL);
	g_return_val_if_fail (trigger != -1, NULL);
	g_return_val_if_fail (alarm_fn != NULL, NULL);

	now = time (NULL);
	if (trigger < now)
		return NULL;

	ar = g_new (AlarmRecord, 1);
	ar->trigger = trigger;
	ar->alarm_fn = alarm_fn;
	ar->data = data;
	ar->destroy_notify_fn = destroy_notify_fn;

	g_print ("alarm_add(): Adding alarm for %s\n", ctime (&trigger));

	if (!queue_alarm (now, ar)) {
		g_free (ar);
		ar = NULL;
	}

	return ar;
}

/**
 * alarm_remove:
 * @alarm: A queued alarm identifier.
 * 
 * Removes an alarm from the alarm queue.
 **/
void
alarm_remove (gpointer alarm)
{
	AlarmRecord *ar;
	AlarmRecord *old_head;
	GList *l;

	g_return_if_fail (alarm_inited);
	g_return_if_fail (alarm != NULL);

	ar = alarm;

	l = g_list_find (alarms, ar);
	if (!l) {
		g_message ("alarm_remove(): Requested removal of nonexistent alarm!");
		return;
	}

	old_head = alarms->data;

	if (old_head == ar)
		pop_alarm ();
	else {
		alarms = g_list_remove_link (alarms, l);
		g_list_free_1 (l);
	}

	if (ar->destroy_notify_fn)
		(* ar->destroy_notify_fn) (ar, ar->data);

	g_free (ar);
}

/**
 * alarm_init:
 *
 * Initializes the alarm timer mechanism.  This must be called near the
 * beginning of the program.
 **/
void
alarm_init (void)
{
	struct sigaction sa;
	int flags;

	g_return_if_fail (alarm_inited == FALSE);

	pipe (alarm_pipes);

	/* set non blocking mode */
	flags = 0;
	fcntl (alarm_pipes [0], F_GETFL, &flags);
	fcntl (alarm_pipes [0], F_SETFL, flags | O_NONBLOCK);
	gdk_input_add (alarm_pipes [0], GDK_INPUT_READ, alarm_ready, NULL);

	/* Setup the signal handler */
	sa.sa_handler = alarm_signal;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction (SIGALRM, &sa, NULL);

	alarm_inited = TRUE;
}

/**
 * alarm_done:
 * 
 * Terminates the alarm timer mechanism.  This should be called at the end of
 * the program.
 **/
void
alarm_done (void)
{
	GList *l;

	g_return_if_fail (alarm_inited);

	if (!clear_itimer ())
		g_message ("alarm_done(): Could not clear the timer!  "
			   "Weird things may happen.");

	for (l = alarms; l; l = l->next) {
		AlarmRecord *ar;

		ar = l->data;

		if (ar->destroy_notify_fn)
			(* ar->destroy_notify_fn) (ar, ar->data);

		g_free (ar);
	}

	g_list_free (alarms);
	alarms = NULL;

	if (close (alarm_pipes[0]) != 0)
		g_message ("alarm_done(): Could not close the input pipe for notification");

	alarm_pipes[0] = -1;

	if (close (alarm_pipes[1]) != 0)
		g_message ("alarm_done(): Could not close the output pipe for notification");

	alarm_pipes[1] = -1;

	alarm_inited = FALSE;
}
