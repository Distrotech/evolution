#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

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
#include "mail-mt.h"

/*#define MALLOC_CHECK*/
#define d(x) 

static void set_stop(int sensitive);
static void mail_enable_stop(void);
static void mail_disable_stop(void);
static void mail_operation_status(struct _CamelOperation *op, const char *what, int pc, void *data);

#define MAIL_MT_LOCK(x) pthread_mutex_lock(&x)
#define MAIL_MT_UNLOCK(x) pthread_mutex_unlock(&x)

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
static unsigned int mail_msg_seq; /* sequence number of each message */
static GHashTable *mail_msg_active; /* table of active messages, must hold mail_msg_lock to access */
static pthread_mutex_t mail_msg_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mail_msg_cond = PTHREAD_COND_INITIALIZER;

pthread_t mail_gui_thread;

static void mail_msg_destroy(EThread *e, EMsg *msg, void *data);

void *mail_msg_new(mail_msg_op_t *ops, EMsgPort *reply_port, size_t size)
{
	struct _mail_msg *msg;

	MAIL_MT_LOCK(mail_msg_lock);

	msg = g_malloc0(size);
	msg->ops = ops;
	msg->seq = mail_msg_seq++;
	msg->msg.reply_port = reply_port;
	msg->cancel = camel_operation_new(mail_operation_status, (void *)msg->seq);
	camel_exception_init(&msg->ex);
	msg->priv = g_malloc0(sizeof(*msg->priv));

	g_hash_table_insert(mail_msg_active, (void *)msg->seq, msg);

	d(printf("New message %p\n", msg));

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

	g_hash_table_remove(mail_msg_active, (void *)m->seq);
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

	camel_operation_unref(m->cancel);
	camel_exception_clear(&m->ex);
	/*g_free(m->priv->what);*/
	g_free(m->priv);
	g_free(m);

	if (activity)
		mail_proxy_event(destroy_objects, NULL, activity, NULL);
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
	m = g_hash_table_lookup(mail_msg_active, (void *)msgid);

	if (m)
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
		m = g_hash_table_lookup(mail_msg_active, (void *)msgid);
		while (m) {
			MAIL_MT_UNLOCK(mail_msg_lock);
			gtk_main_iteration();
			MAIL_MT_LOCK(mail_msg_lock);
			m = g_hash_table_lookup(mail_msg_active, (void *)msgid);
		}
		MAIL_MT_UNLOCK(mail_msg_lock);
	} else {
		MAIL_MT_LOCK(mail_msg_lock);
		m = g_hash_table_lookup(mail_msg_active, (void *)msgid);
		while (m) {
			pthread_cond_wait(&mail_msg_cond, &mail_msg_lock);
			m = g_hash_table_lookup(mail_msg_active, (void *)msgid);
		}
		MAIL_MT_UNLOCK(mail_msg_lock);
	}
}

void mail_msg_wait_all(void)
{
	int ismain = pthread_self() == mail_gui_thread;

	if (ismain) {
		MAIL_MT_LOCK(mail_msg_lock);
		while (g_hash_table_size(mail_msg_active) > 0) {
			MAIL_MT_UNLOCK(mail_msg_lock);
			gtk_main_iteration();
			MAIL_MT_LOCK(mail_msg_lock);
		}
		MAIL_MT_UNLOCK(mail_msg_lock);
	} else {
		MAIL_MT_LOCK(mail_msg_lock);
		while (g_hash_table_size(mail_msg_active) > 0) {
			pthread_cond_wait(&mail_msg_cond, &mail_msg_lock);
		}
		MAIL_MT_UNLOCK(mail_msg_lock);
	}
}

EMsgPort		*mail_gui_port;
static GIOChannel	*mail_gui_channel;
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

		d(printf("message received at thread\n"));
		camel_operation_register(m->cancel);
		camel_operation_start(m->cancel, "%s", text);
		g_free(text);
	}

	if (m->ops->receive_msg) {
		mail_enable_stop();
		m->ops->receive_msg(m);
		mail_disable_stop();
	}

	if (m->ops->describe_msg) {
		camel_operation_end(m->cancel);
		camel_operation_unregister(m->cancel);
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
	g_io_add_watch(mail_gui_channel, G_IO_IN, mail_msgport_received, mail_gui_port);

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

	mail_msg_active = g_hash_table_new(NULL, NULL);
	mail_gui_thread = pthread_self();
}

/* ********************************************************************** */

/* locks */
static pthread_mutex_t status_lock = PTHREAD_MUTEX_INITIALIZER;

/* ********************************************************************** */

struct _pass_msg {
	struct _mail_msg msg;
	const char *prompt;
	gboolean secret;
	gboolean *cache;
	char *result;
	char *service_url;
	GtkWidget *check;
};

static void
pass_got (char *string, void *data)
{
	struct _pass_msg *m = data;
	
	if (string) {
		MailConfigService *service = NULL;
		const MailConfigAccount *mca;
		gboolean remember;
		
		m->result = g_strdup (string);
		
		remember = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (m->check));
		if (m->service_url) {
			mca = mail_config_get_account_by_source_url (m->service_url);
			if (mca) {
				service = mca->source;
			} else {
				mca = mail_config_get_account_by_transport_url (m->service_url);
				if (mca)
					service = mca->transport;
			}
			
			if (service)
				mail_config_service_set_save_passwd (service, remember);
			
			if (mca) {
				mail_config_service_set_save_passwd (service, remember);
				
				/* set `remember' to TRUE because people don't want to have to
				   re-enter their passwords for this session even if they told
				   us not to cache their passwords in the dialog...*sigh* */
				remember = TRUE;
			}
		}
		
		if (m->cache)
			*(m->cache) = remember;
	}
}

static void
do_get_pass (struct _mail_msg *mm)
{
	struct _pass_msg *m = (struct _pass_msg *)mm;
	const MailConfigAccount *mca = NULL;
	GtkWidget *dialogue;
	GtkWidget *check, *entry;
	GList *children, *iter;
	gboolean show;
	char *title;
	
	/* this api is just awful ... hence the hacks */
	dialogue = gnome_request_dialog (m->secret, m->prompt, NULL,
					 0, pass_got, m, NULL);
	
	/* Remember the password? */
	check = gtk_check_button_new_with_label (m->service_url ? _("Remember this password") :
						 _("Remember this password for the remainder of this session"));
	show = TRUE;
	
	if (m->service_url) {
		mca = mail_config_get_account_by_source_url (m->service_url);
		if (mca)
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), mca->source->save_passwd);
		else {
			mca = mail_config_get_account_by_transport_url (m->service_url);
			if (mca)
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), mca->transport->save_passwd);
			else {
				d(printf ("Cannot figure out which account owns URL \"%s\"\n", m->service_url));
				show = FALSE;
			}
		}
	}
	
	if (show)
		gtk_widget_show (check);
	
	/* do some dirty stuff to put the checkbutton after the entry */
	entry = NULL;
	children = gtk_container_children (GTK_CONTAINER (GNOME_DIALOG (dialogue)->vbox));
	
	for (iter = children; iter; iter = iter->next) {
		if (GTK_IS_ENTRY (iter->data)) {
			entry = GTK_WIDGET (iter->data);
			break;
		}
	}
	
	g_list_free (children);
	
	if (entry) {
		gtk_object_ref (GTK_OBJECT (entry));
		gtk_container_remove (GTK_CONTAINER (GNOME_DIALOG (dialogue)->vbox), entry);
	}
	
	gtk_box_pack_end (GTK_BOX (GNOME_DIALOG (dialogue)->vbox),
			  check, TRUE, FALSE, 0);
	
	if (entry) {
		gtk_box_pack_end (GTK_BOX (GNOME_DIALOG (dialogue)->vbox), entry, TRUE, FALSE, 0);
		gtk_widget_grab_focus (entry);
		gtk_object_unref (GTK_OBJECT (entry));
	}
	
	m->check = check;
	
	/* hrm, we can't run this async since the gui_port from which we're called
	   will reply to our message for us */
	
	if (mca) {
		char *name;
		
		name = e_utf8_to_gtk_string (GTK_WIDGET (dialogue), mca->name);
		title = g_strdup_printf (_("Enter Password for %s"), name);
		g_free (name);
	} else
		title = g_strdup (_("Enter Password"));
	
	gtk_window_set_title (GTK_WINDOW (dialogue), title);
	g_free (title);
	
	gnome_dialog_run_and_close ((GnomeDialog *)dialogue);
	
	/*gtk_widget_show(dialogue);*/
}

static void
do_free_pass(struct _mail_msg *mm)
{
	/*struct _pass_msg *m = (struct _pass_msg *)mm;*/
	
	/* the string is passed out so we dont need to free it */
}

struct _mail_msg_op get_pass_op = {
	NULL,
	do_get_pass,
	NULL,
	do_free_pass,
};

/* returns the password, or NULL if cancelled */
char *
mail_get_password (CamelService *service, const char *prompt, gboolean secret, gboolean *cache)
{
	char *ret;
	struct _pass_msg *m, *r;
	EMsgPort *pass_reply;
	
	pass_reply = e_msgport_new ();
	
	m = mail_msg_new (&get_pass_op, pass_reply, sizeof (struct _pass_msg));
	
	m->prompt = prompt;
	m->secret = secret;
	m->cache = cache;
	if (service) {
		m->service_url = camel_url_to_string (service->url, CAMEL_URL_HIDE_ALL);
	} else
		m->service_url = NULL;
	
	if (pthread_self () == mail_gui_thread) {
		do_get_pass ((struct _mail_msg *)m);
		r = m;
	} else {
		static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
		
		/* we want this single-threaded, this is the easiest way to do it without blocking ? */
		pthread_mutex_lock (&lock);
		e_msgport_put (mail_gui_port, (EMsg *)m);
		e_msgport_wait (pass_reply);
		r = (struct _pass_msg *)e_msgport_get (pass_reply);
		pthread_mutex_unlock (&lock);
	}
	
	g_assert (r == m);
	
	ret = m->result;
	
	g_free (m->service_url);
	mail_msg_free (m);
	e_msgport_destroy (pass_reply);
	
	return ret;
}

/* ******************** */

/* ********************************************************************** */

struct _user_message_msg {
	struct _mail_msg msg;
	const char *type;
	const char *prompt;
	gboolean allow_cancel;
	gboolean result;
};

static void
do_user_message (struct _mail_msg *mm)
{
	struct _user_message_msg *m = (struct _user_message_msg *)mm;
	int dialog_result;
	GtkWidget *dialog;
	
	dialog = gnome_message_box_new (m->prompt, m->type,
					GNOME_STOCK_BUTTON_OK,
					m->allow_cancel ? GNOME_STOCK_BUTTON_CANCEL : NULL,
					NULL);
	gnome_dialog_set_default (GNOME_DIALOG (dialog), 1);
	gtk_window_set_policy (GTK_WINDOW (dialog), TRUE, TRUE, TRUE);
	
	/* hrm, we can't run this async since the gui_port from which we're called
	   will reply to our message for us */
	dialog_result = gnome_dialog_run_and_close (GNOME_DIALOG (dialog));

	if (dialog_result == -1 || dialog_result == 1)
		m->result = FALSE;
	else
		m->result = TRUE;
}

struct _mail_msg_op user_message_op = {
	NULL,
	do_user_message,
	NULL,
	NULL,
};

/* prompt the user with a yes/no question and return the response */
gboolean
mail_user_message (const char *type, const char *prompt, gboolean allow_cancel)
{
	struct _user_message_msg *m, *r;
	EMsgPort *user_message_reply;
	gboolean accept;
	
	user_message_reply = e_msgport_new ();
	
	m = mail_msg_new (&user_message_op, user_message_reply, sizeof (*m));
	
	m->type = type;
	m->prompt = prompt;
	m->allow_cancel = allow_cancel;
	
	if (pthread_self () == mail_gui_thread) {
		do_user_message ((struct _mail_msg *)m);
		r = m;
	} else {
		static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
		
		/* we want this single-threaded, this is the easiest way to do it without blocking ? */
		pthread_mutex_lock (&lock);
		e_msgport_put (mail_gui_port, (EMsg *)m);
		e_msgport_wait (user_message_reply);
		r = (struct _user_message_msg *)e_msgport_get (user_message_reply);
		pthread_mutex_unlock (&lock);
	}
	
	g_assert (r == m);
	
	accept = m->result;
	
	mail_msg_free (m);
	e_msgport_destroy (user_message_reply);
	
	return accept;
}

/* ******************** */

struct _proxy_msg {
	struct _mail_msg msg;
	CamelObjectEventHookFunc func;
	CamelObject *o;
	void *event_data;
	void *data;
};

static void
do_proxy_event(struct _mail_msg *mm)
{
	struct _proxy_msg *m = (struct _proxy_msg *)mm;

	m->func(m->o, m->event_data, m->data);
}

struct _mail_msg_op proxy_event_op = {
	NULL,
	do_proxy_event,
	NULL,
	NULL,
};

int mail_proxy_event(CamelObjectEventHookFunc func, CamelObject *o, void *event_data, void *data)
{
	struct _proxy_msg *m;
	int id;
	int ismain = pthread_self() == mail_gui_thread;

	if (ismain) {
		func(o, event_data, data);
		/* id of -1 is 'always finished' */
		return -1;
	} else {
		/* we dont have a reply port for this, we dont care when/if it gets executed, just queue it */
		m = mail_msg_new(&proxy_event_op, NULL, sizeof(*m));
		m->func = func;
		m->o = o;
		m->event_data = event_data;
		m->data = data;
		
		id = m->msg.seq;
		e_msgport_put(mail_gui_port, (EMsg *)m);
		return id;
	}
}

/* ********************************************************************** */
/* locked via status_lock */
static int busy_state;

static void do_set_busy(struct _mail_msg *mm)
{
	set_stop(busy_state > 0);
}

struct _mail_msg_op set_busy_op = {
	NULL,
	do_set_busy,
	NULL,
	NULL,
};

static void mail_enable_stop(void)
{
	struct _mail_msg *m;

	MAIL_MT_LOCK(status_lock);
	busy_state++;
	if (busy_state == 1) {
		m = mail_msg_new(&set_busy_op, NULL, sizeof(*m));
		e_msgport_put(mail_gui_port, (EMsg *)m);
	}
	MAIL_MT_UNLOCK(status_lock);
}

static void mail_disable_stop(void)
{
	struct _mail_msg *m;

	MAIL_MT_LOCK(status_lock);
	busy_state--;
	if (busy_state == 0) {
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
	
	msg = g_hash_table_lookup (mail_msg_active, m->data);
	if (msg == NULL) {
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
		char *clientid, *what;
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
			clientid = g_strdup_printf ("%p", msg);
			if (msg->ops->describe_msg)
				what = msg->ops->describe_msg (msg, FALSE);
			else
				what = _("Working");
			
			if (global_shell_client) {
				activity = evolution_activity_client_new (global_shell_client, clientid,
									  progress_icon, what, TRUE,
									  &display);
			} else {
				activity = NULL;
			}
			
			if (msg->ops->describe_msg)
				g_free (what);
			g_free (clientid);
			
			MAIL_MT_LOCK (mail_msg_lock);
			if (data->activity_state == 3) {
				MAIL_MT_UNLOCK (mail_msg_lock);
				if (activity)
					gtk_object_unref (GTK_OBJECT (activity));
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
	}
	
	activity = data->activity;
	if (activity) {
		gtk_object_ref (GTK_OBJECT (activity));
		MAIL_MT_UNLOCK (mail_msg_lock);
		
		evolution_activity_client_update (activity, out, (double)(pc/100.0));
		gtk_object_unref (GTK_OBJECT (activity));
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
