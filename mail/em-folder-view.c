
#include <gtk/gtkvbox.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkvpaned.h>

#include <libgnomeprintui/gnome-print-dialog.h>

#include <camel/camel-mime-message.h>
#include <camel/camel-stream.h>
#include <camel/camel-stream-filter.h>
#include <camel/camel-mime-filter.h>
#include <camel/camel-mime-filter-tohtml.h>
#include <camel/camel-mime-filter-enriched.h>
#include <camel/camel-multipart.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-url.h>

#include "em-format-html-display.h"
#include "em-format-html-print.h"
#include "em-folder-view.h"
#include "message-list.h"

#include "mail-mt.h"
#include "mail-ops.h"

static void emfv_list_message_selected(MessageList *ml, const char *uid, EMFolderView *emfv);

static void emfv_format_link_clicked(EMFormatHTMLDisplay *efhd, const char *uri, EMFolderView *);
static int emfv_format_popup_event(EMFormatHTMLDisplay *efhd, GdkEventButton *event, const char *uri, CamelMimePart *part, EMFolderView *);

static void emfv_set_folder(EMFolderView *emfv, struct _CamelFolder *folder, const char *uri);

struct _EMFolderViewPrivate {
	int dummy;
};

static GtkVBoxClass *emfv_parent;

static void
emfv_init(GObject *o)
{
	EMFolderView *emfv = (EMFolderView *)o;
	struct _EMFolderViewPrivate *p;

	gtk_box_set_homogeneous (GTK_BOX (emfv), FALSE);

	p = emfv->priv = g_malloc0(sizeof(struct _EMFolderViewPrivate));

	emfv->list = (MessageList *)message_list_new();
	g_signal_connect(emfv->list, "message_selected", G_CALLBACK(emfv_list_message_selected), emfv);

	/* setup selection?  etc? */

	g_signal_connect(emfv->preview, "link_clicked", G_CALLBACK(emfv_format_link_clicked), emfv);
	g_signal_connect(emfv->preview, "popup_event", G_CALLBACK(emfv_format_popup_event), emfv);
}

static void
emfv_finalise(GObject *o)
{
	EMFolderView *emfv = (EMFolderView *)o;

	g_free(emfv->priv);

	((GObjectClass *)emfv_parent)->finalize(o);
}

static void
emfv_class_init(GObjectClass *klass)
{
	klass->finalize = emfv_finalise;

	((EMFolderViewClass *)klass)->set_folder = emfv_set_folder;
}

GType
em_folder_view_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMFolderViewClass),
			NULL, NULL,
			(GClassInitFunc)emfv_class_init,
			NULL, NULL,
			sizeof(EMFolderView), 0,
			(GInstanceInitFunc)emfv_init
		};
		emfv_parent = g_type_class_ref(gtk_vbox_get_type());
		type = g_type_register_static(gtk_vbox_get_type(), "EMFolderView", &info, 0);
	}

	return type;
}

GtkWidget *em_folder_view_new(void)
{
	EMFolderView *emfv = g_object_new(em_folder_view_get_type(), 0);

	return (GtkWidget *)emfv;
}

static void
emfv_set_folder(EMFolderView *emfv, struct _CamelFolder *folder, const char *uri)
{
	if (emfv->preview)
		em_format_format((EMFormat *)emfv->preview, NULL);

	/* FIXME: outgoing folder type */
	message_list_set_folder(emfv->list, folder, FALSE);
	g_free(emfv->folder_uri);
	emfv->folder_uri = g_strdup(uri);
	if (folder)
		camel_object_ref(folder);
	if (emfv->folder)
		camel_object_unref(emfv->folder);
	emfv->folder = folder;
}

void em_folder_view_set_folder(EMFolderView *emfv, const char *uri)
{
	/*struct _EMFolderViewPrivate *p = emfv->priv;*/

	((EMFolderViewClass *)G_OBJECT_GET_CLASS(emfv))->set_folder(emfv, uri);
}

static void
emfv_set_message(EMFolderView *emfv, const char *uid);
{
	message_list_select_uid(emfv->list, uid);
}

/* should this not exist/be done via message_list api directly? */
void em_folder_view_set_message(EMFolderView *emfv, const char *uid)
{
	((EMFolderViewClass *)G_OBJECT_GET_CLASS(emfv))->set_message(emfv, uid);
}

int em_folder_view_print(EMFolderView *emfv, int preview)
{
	struct _EMFolderViewPrivate *p = emfv->priv;
	EMFormatHTMLPrint *print;
	GnomePrintConfig *config = NULL;
	int res;
	struct _CamelMedium *msg;

	/* FIXME: need to load the message first */
	if (emfv->preview == NULL)
		return 0;

	msg = emfv->preview->formathtml.format.message;
	if (msg == NULL)
		return 0;
	
	if (!preview) {
		GtkDialog *dialog = (GtkDialog *)gnome_print_dialog_new(NULL, _("Print Message"), GNOME_PRINT_DIALOG_COPIES);

		gtk_dialog_set_default_response(dialog, GNOME_PRINT_DIALOG_RESPONSE_PRINT);
		gtk_window_set_transient_for((GtkWindow *)dialog, (GtkWindow *)gtk_widget_get_toplevel((GtkWidget *)emfv));
		
		switch (gtk_dialog_run(dialog)) {
		case GNOME_PRINT_DIALOG_RESPONSE_PRINT:
			break;
		case GNOME_PRINT_DIALOG_RESPONSE_PREVIEW:
			preview = TRUE;
			break;
		default:
			gtk_widget_destroy((GtkWidget *)dialog);
			return 0;
		}
		
		config = gnome_print_dialog_get_config((GnomePrintDialog *)dialog);
		gtk_widget_destroy((GtkWidget *)dialog);
	}

	print = em_format_html_print_new();
	em_format_format_clone((EMFormat *)print, msg, (EMFormat *)emfv->preview);
	res = em_format_html_print_print(print, config, preview);
	g_object_unref(print);

	return res;
}

static void
emfv_list_done_message_selected(CamelFolder *folder, const char *uid, CamelMimeMessage *msg, void *data)
{
	EMFolderView *emfv = data;

	/* FIXME: mark_seen timeout */
	/* FIXME: asynchronous stuff */

	emf_format_format(emfv->preview, (struct _CamelMedium *)msg);
}

static void
emfv_list_message_selected(MessageList *ml, const char *uid, EMFolderView *emfv)
{
	printf("message selected '%s'\n", uid);

	/* FIXME: ui stuff based on messageinfo, if available */

	if (emfv->preview) {
		mail_get_message(emfv->folder, uid, emfv_list_done_message_selected, emfv, mail_thread_new);
	}
}

static void
emfv_format_link_clicked(EMFormatHTMLDisplay *efhd, const char *uri, EMFolderView *emfv)
{
	printf("Link clicked: %s\n", uri);
}

static int
emfv_format_popup_event(EMFormatHTMLDisplay *efhd, GdkEventButton *event, const char *uri, CamelMimePart *part, EMFolderView *emfv)
{
	printf("popup event, uri '%s', part '%p'\n", uri, part);

	return FALSE;
}
