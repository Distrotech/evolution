#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include <glib.h>

#include <gtk/gtkentry.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkcheckbutton.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-stock.h>
#include <gal/widgets/e-gui-utils.h>
#include <gal/widgets/e-unicode.h>

#include "folder-browser-factory.h"
#include "e-util/e-msgport.h"
#include "camel/camel-operation.h"

#include "evolution-activity-client.h"

#include "mail-config.h"
#include "camel/camel-url.h"
#include "mail-session.h"
#include "mail-mt.h"

#include "component-factory.h"

/*#define MALLOC_CHECK*/
#define LOG_OPS
#define LOG_LOCKS
#define d(x) 

static void set_stop(int sensitive);
static void mail_operation_status(struct _CamelOperation *op, const char *what, int pc, void *data);

#ifdef LOG_LOCKS
#define MAIL_MT_LOCK(x) (log_locks?fprintf(log, "%ld: lock " # x "\n", pthread_self()):0, pthread_mutex_lock(&x))
#define MAIL_MT_UNLOCK(x) (log_locks?fprintf(log, "%ld: unlock " # x "\n", pthread_self()): 0, pthread_mutex_unlock(&x))
#else
#define MAIL_MT_LOCK(x) pthread_mutex_lock(&x)
#define MAIL_MT_UNLOCK(x) pthread_mutex_unlock(&x)
#endif
extern EvolutionShellClient *global_shell_client;

/* background operation status stuff */
struct _mail_msg_priv {
	int activity_state;	/* sigh sigh sigh, we need to keep track of the state external to the
				 pointer itself for locking/race conditions */
	EvolutionActivityClient *activity;
};

/* This is used for the mail status bar, cheap and easy */
#include "art/mail-new.xpm"

static GdkPixbuf *progress_icon[2] = { NULL, NULL };

/* mail_msg stuff */
#ifdef LOG_OPS
static FILE *log;
static int log_ops, log_locks, log_init;
#endif

static unsigned int mail_msg_seq; /* sequence number of each message */
static GHashTable *mail_msg_active_table; /* table of active messages, must hold mail_msg_lock to access */
static pthread_mutex_t mail_msg_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mail_msg_cond = PTHREAD_COND_INITIALIZER;

pthread_t mail_gui_thread;

MailAsyncEvent *mail_async_event;

static void mail_msg_destroy(EThread *e, EMsg *msg, void *data);

void *mail_msg_new(mail_msg_op_t *ops, EMsgPort *reply_port, size_t size)
{
	struct _mail_msg *msg;

	MAIL_MT_LOCK(mail_msg_lock);

#if defined(LOG_OPS) || defined(LOG_LOCKS)
	if (!log_init) {
		time_t now = time(0);

		log_init = TRUE;
		log_ops = getenv("EVOLUTION_MAIL_LOG_OPS") != NULL;
		log_locks = getenv("EVOLUTION_MAIL_LOG_LOCKS") != NULL;
		if (log_ops || log_locks) {
			log = fopen("evolution-mail-ops.log", "w+");
			if (log) {
				setvbuf(log, NULL, _IOLBF, 0);
				fprintf(log, "Started evolution-mail: %s\n", ctime(&now));
				g_warning("Logging mail operations to evolution-mail-ops.log");

				if (log_ops)
					fprintf(log, "Logging async operations\n");

				if (log_locks) {
					fprintf(log, "Logging lock operations, mail_gui_thread = %ld\n\n", mail_gui_thread);
					fprintf(log, "%ld: lock mail_msg_lock\n", pthread_self());
				}
			} else {
				g_warning ("Could not open log file: %s", strerror(errno));
				log_ops = log_locks = FALSE;
			}
		}
	}
#endif
	msg = g_malloc0(size);
	msg->ops = ops;
	msg->seq = mail_msg_seq++;
	msg->msg.reply_port = reply_port;
	msg->cancel = camel_operation_new(mail_operation_status, (void *)msg->seq);
	camel_exception_init(&msg->ex);
	msg->priv = g_malloc0(sizeof(*msg->priv));

	g_hash_table_insert(mail_msg_active_table, (void *)msg->seq, msg);

	d(printf("New message %p\n", msg));

#ifdef LOG_OPS
	if (log_ops)
		fprintf(log, "%p: New\n", msg);
#endif
	MAIL_MT_UNLOCK(mail_msg_lock);

	return msg;
}

/* either destroy the progress (in event_data), or the whole dialogue (in data) */
static void destroy_objects(CamelObject *o, void *event_data, void *data)
{
	if (event_data)
		gtk_object_unref(event_data);
}

#ifdef MALLOC_CHECK
#include <mcheck.h>

static void
checkmem(void *p)
{
	if (p) {
		int status = mprobe(p);

		switch (status) {
		case MCHECK_HEAD:
			printf("Memory underrun at %p\n", p);
			abort();
		case MCHECK_TAIL:
			printf("Memory overrun at %p\n", p);
			abort();
		case MCHECK_FREE:
			printf("Double free %p\n", p);
			abort();
		}
	}
}
#endif

void mail_msg_free(void *msg)
{
	struct _mail_msg *m = msg;
	void *activity = NULL;

#ifdef MALLOC_CHECK
	checkmem(m);
	checkmem(m->cancel);
	checkmem(m->priv);
#endif
	d(printf("Free message %p\n", msg));

	if (m->ops->destroy_msg)
		m->ops->destroy_msg(m);

	MAIL_MT_LOCK(mail_msg_lock);

#ifdef LOG_OPS
	if (log_ops)
		fprintf(log, "%p: Free  (exception `%s')\n", msg,
			camel_exception_get_description(&m->ex)?camel_exception_get_description(&m->ex):"None");
#endif
	g_hash_table_remove(mail_msg_active_table, (void *)m->seq);
	pthread_cond_broadcast(&mail_msg_cond);

	/* We need to make sure we dont lose a reference here YUCK YUCK */
	/* This is tightly integrated with the code in do_op_status,
	   as it closely relates to the CamelOperation setup in msg_new() above */
	if (m->priv->activity_state == 1) {
		m->priv->activity_state = 3; /* tell the other thread
					      * to free it itself (yuck yuck) */
		MAIL_MT_UNLOCK(mail_msg_lock);
		return;
	} else {
		activity = m->priv->activity;
	}

	MAIL_MT_UNLOCK(mail_msg_lock);

	if (m->cancel)
		camel_operation_unref(m->cancel);

	camel_exception_clear(&m->ex);
	/*g_free(m->priv->what);*/
	g_free(m->priv);
	g_free(m);

	if (activity)
		mail_async_event_emit(mail_async_event, MAIL_ASYNC_GUI, (MailAsyncFunc)destroy_objects, NULL, activity, NULL);
}

/* hash table of ops->dialogue of active errors */
static GHashTable *active_errors = NULL;

static void error_gone(GtkObject *o, void *data)
{
	g_hash_table_remove(active_errors, data);
}

void mail_msg_check_error(void *msg)
{
	struct _mail_msg *m = msg;
	char *what = NULL;
	char *text;
	GnomeDialog *gd;
	
#ifdef MALLOC_CHECK
	checkmem(m);
	checkmem(m->cancel);
	checkmem(m->priv);
#endif
	
	/* don't report any errors if we are not in interactive mode */
	if (!mail_session_get_interactive ())
		return;
	
	if (!camel_exception_is_set(&m->ex)
	    || m->ex.id == CAMEL_EXCEPTION_USER_CANCEL)
		return;

	if (active_errors == NULL)
		active_errors = g_hash_table_new(NULL, NULL);

	if (m->ops->describe_msg)
		what = m->ops->describe_msg(m, FALSE);

	if (what) {
		text = g_strdup_printf(_("Error while '%s':\n%s"), what, camel_exception_get_description(&m->ex));
		g_free (what);
	} else
		text = g_strdup_printf(_("Error while performing operation:\n%s"), camel_exception_get_description(&m->ex));

	/* check to see if we have dialogue already running for this operation */
	/* we key on the operation pointer, which is at least accurate enough
	   for the operation type, although it could be on a different object. */
	if (g_hash_table_lookup(active_errors, m->ops)) {
		g_warning("Error occured while existing dialogue active:\n%s", text);
		g_free(text);
		return;
	}

	gd = (GnomeDialog *)gnome_error_dialog(text);
	g_hash_table_insert(active_errors, m->ops, gd);
	g_free(text);
	gtk_signal_connect((GtkObject *)gd, "destroy", error_gone, m->ops);
	gnome_dialog_set_close(gd, TRUE);
	gtk_widget_show((GtkWidget *)gd);
}

void mail_msg_cancel(unsigned int msgid)
{
	struct _mail_msg *m;

	MAIL_MT_LOCK(mail_msg_lock);
	m = g_hash_table_lookup(mail_msg_active_table, (void *)msgid);

	if (m && m->cancel)
		camel_operation_cancel(m->cancel);

	MAIL_MT_UNLOCK(mail_msg_lock);
}


/* waits for a message to be finished processing (freed)
   the messageid is from struct _mail_msg->seq */
void mail_msg_wait(unsigned int msgid)
{
	struct _mail_msg *m;
	int ismain = pthread_self() == mail_gui_thread;

	if (ismain) {
		MAIL_MT_LOCK(mail_msg_lock);
		m = g_hash_table_lookup(mail_msg_active_table, (void *)msgid);
		while (m) {
			MAIL_MT_UNLOCK(mail_msg_lock);
			gtk_main_iteration();
			MAIL_MT_LOCK(mail_msg_lock);
			m = g_hash_table_lookup(mail_msg_active_table, (void *)msgid);
		}
		MAIL_MT_UNLOCK(mail_msg_lock);
	} else {
		MAIL_MT_LOCK(mail_msg_lock);
		m = g_hash_table_lookup(mail_msg_active_table, (void *)msgid);
		while (m) {
			pthread_cond_wait(&mail_msg_cond, &mail_msg_lock);
			m = g_hash_table_lookup(mail_msg_active_table, (void *)msgid);
		}
		MAIL_MT_UNLOCK(mail_msg_lock);
	}
}

int mail_msg_active(unsigned int msgid)
{
	int active;

	MAIL_MT_LOCK(mail_msg_lock);
	if (msgid == (unsigned int)-1) 
		active = g_hash_table_size(mail_msg_active_table) > 0;
	else
		active = g_hash_table_lookup(mail_msg_active_table, (void *)msgid) != NULL;
	MAIL_MT_UNLOCK(mail_msg_lock);

	return active;
}

void mail_msg_wait_all(void)
{
	int ismain = pthread_self() == mail_gui_thread;

	if (ismain) {
		MAIL_MT_LOCK(mail_msg_lock);
		while (g_hash_table_size(mail_msg_active_table) > 0) {
			MAIL_MT_UNLOCK(mail_msg_lock);
			gtk_main_iteration();
			MAIL_MT_LOCK(mail_msg_lock);
		}
		MAIL_MT_UNLOCK(mail_msg_lock);
	} else {
		MAIL_MT_LOCK(mail_msg_lock);
		while (g_hash_table_size(mail_msg_active_table) > 0) {
			pthread_cond_wait(&mail_msg_cond, &mail_msg_lock);
		}
		MAIL_MT_UNLOCK(mail_msg_lock);
	}
}

EMsgPort		*mail_gui_port;
static GIOChannel	*mail_gui_channel;
static guint		 mail_gui_watch;

/* TODO: Merge these, gui_port2 doesn't do any mail_msg processing on the request (replies, forwards, frees) */
EMsgPort		*mail_gui_port2;
static GIOChannel	*mail_gui_channel2;
static guint		 mail_gui_watch2;

EMsgPort		*mail_gui_reply_port;
static GIOChannel	*mail_gui_reply_channel;

/* a couple of global threads available */
EThread *mail_thread_queued;	/* for operations that can (or should) be queued */
EThread *mail_thread_queued_slow;	/* for operations that can (or should) be queued, but take a long time */
EThread *mail_thread_new;	/* for operations that should run in a new thread each time */

static gboolean
mail_msgport_replied(GIOChannel *source, GIOCondition cond, void *d)
{
	EMsgPort *port = (EMsgPort *)d;
	mail_msg_t *m;

	while (( m = (mail_msg_t *)e_msgport_get(port))) {

#ifdef MALLOC_CHECK
		checkmem(m);
		checkmem(m->cancel);
		checkmem(m->priv);
#endif

#ifdef LOG_OPS
		if (log_ops)
			fprintf(log, "%p: Replied to GUI thread (exception `%s'\n", m,
				camel_exception_get_description(&m->ex)?camel_exception_get_description(&m->ex):"None");
#endif

		if (m->ops->reply_msg)
			m->ops->reply_msg(m);
		mail_msg_check_error(m);
		mail_msg_free(m);
	}

	return TRUE;
}

static gboolean
mail_msgport_received(GIOChannel *source, GIOCondition cond, void *d)
{
	EMsgPort *port = (EMsgPort *)d;
	mail_msg_t *m;

	while (( m = (mail_msg_t *)e_msgport_get(port))) {
#ifdef MALLOC_CHECK
		checkmem(m);
		checkmem(m->cancel);
		checkmem(m->priv);
#endif

#ifdef LOG_OPS
		if (log_ops)
			fprintf(log, "%p: Received at GUI thread\n", m);
#endif

		if (m->ops->receive_msg)
			m->ops->receive_msg(m);
		if (m->msg.reply_port)
			e_msgport_reply((EMsg *)m);
		else {
			if (m->ops->reply_msg)
				m->ops->reply_msg(m);
			mail_msg_free(m);
		}
	}

	return TRUE;
}

/* Test code, lighterwight, more configurable calls */
static gboolean
mail_msgport_received2(GIOChannel *source, GIOCondition cond, void *d)
{
	EMsgPort *port = (EMsgPort *)d;
	mail_msg_t *m;

	while (( m = (mail_msg_t *)e_msgport_get(port))) {
#ifdef LOG_OPS
		if (log_ops)
			fprintf(log, "%p: Received at GUI2 thread\n", m);
#endif

		if (m->ops->receive_msg)
			m->ops->receive_msg(m);
		else
			mail_msg_free(m);
	}

	return TRUE;
}


static void
mail_msg_destroy(EThread *e, EMsg *msg, void *data)
{
	mail_msg_t *m = (mail_msg_t *)msg;

#ifdef MALLOC_CHECK
	checkmem(m);
	checkmem(m->cancel);
	checkmem(m->priv);
#endif	

	mail_msg_free(m);
}

static void
mail_msg_received(EThread *e, EMsg *msg, void *data)
{
	mail_msg_t *m = (mail_msg_t *)msg;

#ifdef MALLOC_CHECK
	checkmem(m);
	checkmem(m->cancel);
	checkmem(m->priv);
#endif

	if (m->ops->describe_msg) {
		char *text = m->ops->describe_msg(m, FALSE);

#ifdef LOG_OPS
		if (log_ops)
			fprintf(log, "%p: Received at thread %ld: '%s'\n", m, pthread_self(), text);
#endif

		d(printf("message received at thread\n"));
		camel_operation_register(m->cancel);
		camel_operation_start(m->cancel, "%s", text);
		g_free(text);
	}
#ifdef LOG_OPS
	else
		if (log_ops)
			fprintf(log, "%p: Received at thread %ld\n", m, pthread_self());
#endif

	if (m->ops->receive_msg) {
		mail_enable_stop();
		m->ops->receive_msg(m);
		mail_disable_stop();
	}

	if (m->ops->describe_msg) {
		camel_operation_end(m->cancel);
		camel_operation_unregister(m->cancel);
		MAIL_MT_LOCK(mail_msg_lock);
		camel_operation_unref(m->cancel);
		m->cancel = NULL;
		MAIL_MT_UNLOCK(mail_msg_lock);
	}
}

void mail_msg_cleanup(void)
{
	mail_msg_wait_all();

	e_thread_destroy(mail_thread_queued_slow);
	e_thread_destroy(mail_thread_queued);
	e_thread_destroy(mail_thread_new);

	g_io_channel_unref(mail_gui_channel);
	g_io_channel_unref(mail_gui_reply_channel);

	e_msgport_destroy(mail_gui_port);
	e_msgport_destroy(mail_gui_reply_port);
}

void mail_msg_init(void)
{
	mail_gui_reply_port = e_msgport_new();
	mail_gui_reply_channel = g_io_channel_unix_new(e_msgport_fd(mail_gui_reply_port));
	g_io_add_watch(mail_gui_reply_channel, G_IO_IN, mail_msgport_replied, mail_gui_reply_port);

	mail_gui_port = e_msgport_new();
	mail_gui_channel = g_io_channel_unix_new(e_msgport_fd(mail_gui_port));
	mail_gui_watch = g_io_add_watch(mail_gui_channel, G_IO_IN, mail_msgport_received, mail_gui_port);

	/* experimental temporary */
	mail_gui_port2 = e_msgport_new();
	mail_gui_channel2 = g_io_channel_unix_new(e_msgport_fd(mail_gui_port2));
	mail_gui_watch2 = g_io_add_watch(mail_gui_channel2, G_IO_IN, mail_msgport_received2, mail_gui_port2);


	mail_thread_queued = e_thread_new(E_THREAD_QUEUE);
	e_thread_set_msg_destroy(mail_thread_queued, mail_msg_destroy, 0);
	e_thread_set_msg_received(mail_thread_queued, mail_msg_received, 0);
	e_thread_set_reply_port(mail_thread_queued, mail_gui_reply_port);

	mail_thread_queued_slow = e_thread_new(E_THREAD_QUEUE);
	e_thread_set_msg_destroy(mail_thread_queued_slow, mail_msg_destroy, 0);
	e_thread_set_msg_received(mail_thread_queued_slow, mail_msg_received, 0);
	e_thread_set_reply_port(mail_thread_queued_slow, mail_gui_reply_port);

	mail_thread_new = e_thread_new(E_THREAD_NEW);
	e_thread_set_msg_destroy(mail_thread_new, mail_msg_destroy, 0);
	e_thread_set_msg_received(mail_thread_new, mail_msg_received, 0);
	e_thread_set_reply_port(mail_thread_new, mail_gui_reply_port);
	e_thread_set_queue_limit(mail_thread_new, 10);

	mail_msg_active_table = g_hash_table_new(NULL, NULL);
	mail_gui_thread = pthread_self();

	mail_async_event = mail_async_event_new();
}

/* ********************************************************************** */

/* locks */
static pthread_mutex_t status_lock = PTHREAD_MUTEX_INITIALIZER;

/* ********************************************************************** */

struct _proxy_msg {
	struct _mail_msg msg;
	MailAsyncEvent *ea;
	mail_async_event_t type;

	pthread_t thread;

	MailAsyncFunc func;
	void *o;
	void *event_data;
	void *data;
};

static void
do_async_event(struct _mail_msg *mm)
{
	struct _proxy_msg *m = (struct _proxy_msg *)mm;

	m->thread = pthread_self();
	m->func(m->o, m->event_data, m->data);
	m->thread = ~0;

	g_mutex_lock(m->ea->lock);
	m->ea->tasks = g_slist_remove(m->ea->tasks, m);
	g_mutex_unlock(m->ea->lock);
}

static int
idle_async_event(void *mm)
{
	do_async_event(mm);
	mail_msg_free(mm);

	return FALSE;
}

struct _mail_msg_op async_event_op = {
	NULL,
	do_async_event,
	NULL,
	NULL,
};

MailAsyncEvent *mail_async_event_new(void)
{
	MailAsyncEvent *ea;

	ea = g_malloc0(sizeof(*ea));
	ea->lock = g_mutex_new();

	return ea;
}

int mail_async_event_emit(MailAsyncEvent *ea, mail_async_event_t type, MailAsyncFunc func, void *o, void *event_data, void *data)
{
	struct _proxy_msg *m;
	int id;
	int ismain = pthread_self() == mail_gui_thread;

	/* we dont have a reply port for this, we dont care when/if it gets executed, just queue it */
	m = mail_msg_new(&async_event_op, NULL, sizeof(*m));
	m->func = func;
	m->o = o;
	m->event_data = event_data;
	m->data = data;
	m->ea = ea;
	m->type = type;
	m->thread = ~0;
	
	id = m->msg.seq;
	g_mutex_lock(ea->lock);
	ea->tasks = g_slist_prepend(ea->tasks, m);
	g_mutex_unlock(ea->lock);

	/* We use an idle function instead of our own message port only because the
	   gui message ports's notification buffer might overflow and deadlock us */
	if (type == MAIL_ASYNC_GUI) {
		if (ismain)
			g_idle_add(idle_async_event, m);
		else
			e_msgport_put(mail_gui_port, (EMsg *)m);
	} else
		e_thread_put(mail_thread_queued, (EMsg *)m);

	return id;
}

int mail_async_event_destroy(MailAsyncEvent *ea)
{
	int id;
	pthread_t thread = pthread_self();
	struct _proxy_msg *m;

	g_mutex_lock(ea->lock);
	while (ea->tasks) {
		m = ea->tasks->data;
		id = m->msg.seq;
		if (m->thread == thread) {
			g_warning("Destroying async event from inside an event, returning EDEADLK");
			g_mutex_unlock(ea->lock);
			errno = EDEADLK;
			return -1;
		}
		g_mutex_unlock(ea->lock);
		mail_msg_wait(id);
		g_mutex_lock(ea->lock);
	}
	g_mutex_unlock(ea->lock);

	g_mutex_free(ea->lock);
	g_free(ea);

	return 0;
}

/* ********************************************************************** */

struct _call_msg {
	struct _mail_msg msg;
	mail_call_t type;
	MailMainFunc func;
	void *ret;
	va_list ap;
};

static void
do_call(struct _mail_msg *mm)
{
	struct _call_msg *m = (struct _call_msg *)mm;
	void *p1, *p2, *p3, *p4, *p5;
	int i1;
	va_list ap;

	G_VA_COPY(ap, m->ap);

	switch(m->type) {
	case MAIL_CALL_p_p:
		p1 = va_arg(ap, void *);
		m->ret = m->func(p1);
		break;
	case MAIL_CALL_p_pp:
		p1 = va_arg(ap, void *);
		p2 = va_arg(ap, void *);
		m->ret = m->func(p1, p2);
		break;
	case MAIL_CALL_p_ppp:
		p1 = va_arg(ap, void *);
		p2 = va_arg(ap, void *);
		p3 = va_arg(ap, void *);
		m->ret = m->func(p1, p2, p3);
		break;
	case MAIL_CALL_p_pppp:
		p1 = va_arg(ap, void *);
		p2 = va_arg(ap, void *);
		p3 = va_arg(ap, void *);
		p4 = va_arg(ap, void *);
		m->ret = m->func(p1, p2, p3, p4);
		break;
	case MAIL_CALL_p_ppippp:
		p1 = va_arg(ap, void *);
		p2 = va_arg(ap, void *);
		i1 = va_arg(ap, int);
		p3 = va_arg(ap, void *);
		p4 = va_arg(ap, void *);
		p5 = va_arg(ap, void *);
		m->ret = m->func(p1, p2, i1, p3, p4, p5);
		break;
	}
}

struct _mail_msg_op mail_call_op = {
	NULL,
	do_call,
	NULL,
	NULL,
};

void *mail_call_main(mail_call_t type, MailMainFunc func, ...)
{
	struct _call_msg *m;
	void *ret;
	va_list ap;
	EMsgPort *reply = NULL;
	int ismain = pthread_self() == mail_gui_thread;

	va_start(ap, func);

	if (!ismain)
		reply = e_msgport_new();

	m = mail_msg_new(&mail_call_op, reply, sizeof(*m));
	m->type = type;
	m->func = func;
	G_VA_COPY(m->ap, ap);

	if (!ismain) {
		e_msgport_put(mail_gui_port, (EMsg *)m);
		e_msgport_wait(reply);
		e_msgport_destroy(reply);
	} else {
		do_call(&m->msg);
	}

	va_end(ap);

	ret = m->ret;
	mail_msg_free(m);
	
	return ret;
}

/* ********************************************************************** */
/* locked via status_lock */
static int busy_state;

static void do_set_busy(struct _mail_msg *mm)
{
	if (global_shell_client)
		set_stop(busy_state > 0);
}

struct _mail_msg_op set_busy_op = {
	NULL,
	do_set_busy,
	NULL,
	NULL,
};

void mail_enable_stop(void)
{
	struct _mail_msg *m;

	MAIL_MT_LOCK(status_lock);
	busy_state++;
	if (busy_state == 1 && global_shell_client) {
		m = mail_msg_new(&set_busy_op, NULL, sizeof(*m));
		e_msgport_put(mail_gui_port, (EMsg *)m);
	}
	MAIL_MT_UNLOCK(status_lock);
}

void mail_disable_stop(void)
{
	struct _mail_msg *m;

	MAIL_MT_LOCK(status_lock);
	busy_state--;
	if (busy_state == 0 && global_shell_client) {
		m = mail_msg_new(&set_busy_op, NULL, sizeof(*m));
		e_msgport_put(mail_gui_port, (EMsg *)m);
	}
	MAIL_MT_UNLOCK(status_lock);
}

/* ******************************************************************************** */

struct _op_status_msg {
	struct _mail_msg msg;

	struct _CamelOperation *op;
	char *what;
	int pc;
	void *data;
};

static void do_op_status(struct _mail_msg *mm)
{
	struct _op_status_msg *m = (struct _op_status_msg *)mm;
	struct _mail_msg *msg;
	struct _mail_msg_priv *data;
	char *out, *p, *o, c;
	int pc;
	EvolutionActivityClient *activity;
	
	g_assert (mail_gui_thread == pthread_self ());
	
	MAIL_MT_LOCK (mail_msg_lock);
	
	msg = g_hash_table_lookup (mail_msg_active_table, m->data);

	/* shortcut processing, i.e. if we have no global_shell_client and no activity, we can't create one */
	if (msg == NULL || (msg->priv->activity == NULL && global_shell_client == NULL)) {
		MAIL_MT_UNLOCK (mail_msg_lock);
		return;
	}
	
	data = msg->priv;
	
	out = alloca (strlen (m->what) * 2 + 1);
	o = out;
	p = m->what;
	while ((c = *p++)) {
		if (c == '%')
			*o++ = '%';
		*o++ = c;
	}
	*o = 0;
	
	pc = m->pc;
	
	/* so whats all this crap about:
	 * When we call activity_client, we have a chance of coming
	 * back to code that will call mail_msg_new or one of many
	 * calls which may deadlock us.  So we need to call corba
	 * outside of the lock.  The activity_state thing is so we can
	 * properly lock data->activity without having to hold a lock
	 * ... of course we have to be careful in the free function to
	 * keep track of it too.
	 */
	if (data->activity == NULL && global_shell_client) {
		char *what;
		int display;
		
		/* its being created/removed?  well leave it be */
		if (data->activity_state == 1 || data->activity_state == 3) {
			MAIL_MT_UNLOCK (mail_msg_lock);
			return;
		} else {
			data->activity_state = 1;
			
			if (progress_icon[0] == NULL)
				progress_icon[0] = gdk_pixbuf_new_from_xpm_data ((const char **)mail_new_xpm);
			
			MAIL_MT_UNLOCK (mail_msg_lock);
			if (msg->ops->describe_msg)
				what = msg->ops->describe_msg (msg, FALSE);
			else
				what = _("Working");
			
			if (global_shell_client) {
				activity = evolution_activity_client_new (global_shell_client,
									  COMPONENT_ID,
									  progress_icon, what, TRUE,
									  &display);
			} else {
				activity = NULL;
			}
			
			if (msg->ops->describe_msg)
				g_free (what);
			
			MAIL_MT_LOCK (mail_msg_lock);
			if (data->activity_state == 3) {
				MAIL_MT_UNLOCK (mail_msg_lock);
				if (activity)
					gtk_object_unref (GTK_OBJECT (activity));
				if (msg->cancel)
					camel_operation_unref (msg->cancel);
				camel_exception_clear (&msg->ex);
				g_free (msg->priv);
				g_free (msg);
			} else {
				data->activity_state = 2;
				data->activity = activity;
				MAIL_MT_UNLOCK (mail_msg_lock);
			}
			return;
		}
	} else if (data->activity) {
		activity = data->activity;
		gtk_object_ref (GTK_OBJECT (activity));
		MAIL_MT_UNLOCK (mail_msg_lock);
			
		evolution_activity_client_update (activity, out, (double)(pc/100.0));
		gtk_object_unref (GTK_OBJECT (activity));
	} else {
		MAIL_MT_UNLOCK (mail_msg_lock);
	}
}

static void
do_op_status_free (struct _mail_msg *mm)
{
	struct _op_status_msg *m = (struct _op_status_msg *)mm;

	g_free (m->what);
}

struct _mail_msg_op op_status_op = {
	NULL,
	do_op_status,
	NULL,
	do_op_status_free,
};

static void
mail_operation_status (struct _CamelOperation *op, const char *what, int pc, void *data)
{
	struct _op_status_msg *m;
	
	d(printf("got operation statys: %s %d%%\n", what, pc));

	if (global_shell_client == NULL)
		return;
	
	m = mail_msg_new(&op_status_op, NULL, sizeof(*m));
	m->op = op;
	m->what = g_strdup(what);
	switch (pc) {
	case CAMEL_OPERATION_START:
		pc = 0;
		break;
	case CAMEL_OPERATION_END:
		pc = 100;
		break;
	}
	m->pc = pc;
	m->data = data;
	e_msgport_put(mail_gui_port, (EMsg *)m);
}

/* ******************** */

static void
set_stop(int sensitive)
{
	EList *controls;
	EIterator *it;
	static int last = FALSE;

	if (last == sensitive)
		return;

	controls = folder_browser_factory_get_control_list ();
	for (it = e_list_get_iterator (controls); e_iterator_is_valid (it); e_iterator_next (it)) {
		BonoboControl *control;
		BonoboUIComponent *uic;

		control = BONOBO_CONTROL (e_iterator_get (it));
		uic = bonobo_control_get_ui_component (control);
		if (uic == CORBA_OBJECT_NIL || bonobo_ui_component_get_container(uic) == CORBA_OBJECT_NIL)
			continue;

		bonobo_ui_component_set_prop(uic, "/commands/MailStop", "sensitive", sensitive?"1":"0", NULL);
	}
	gtk_object_unref(GTK_OBJECT(it));
	last = sensitive;
}
