/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#ifndef _MAIL_DISPLAY_H_
#define _MAIL_DISPLAY_H_

#include <gtk/gtkvbox.h>
#include <gtkhtml/gtkhtml.h>
#include <gtkhtml/gtkhtml-stream.h>

#include <gal/widgets/e-scroll-frame.h>

#include <camel/camel-stream.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-medium.h>
#include <camel/camel-folder.h>

#include "mail-types.h"
#include "mail-config.h" /*display_style*/

#define MAIL_DISPLAY_TYPE        (mail_display_get_type ())
#define MAIL_DISPLAY(o)          (GTK_CHECK_CAST ((o), MAIL_DISPLAY_TYPE, MailDisplay))
#define MAIL_DISPLAY_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), MAIL_DISPLAY_TYPE, MailDisplayClass))
#define IS_MAIL_DISPLAY(o)       (GTK_CHECK_TYPE ((o), MAIL_DISPLAY_TYPE))
#define IS_MAIL_DISPLAY_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), MAIL_DISPLAY_TYPE))

struct _MailDisplay {
	GtkVBox parent;
	
	struct _MailDisplayPrivate *priv;
	
	EScrollFrame *scroll;
	GtkHTML *html;
	/* GtkHTMLStream *stream; */
	gint redisplay_counter;
	gpointer last_active;
	guint idle_id;
	
	char *charset;
	
	char *selection;
	
	CamelMimeMessage *current_message;
	CamelMessageInfo *info;
	CamelFolder *folder;
	GData **data;
	
	/* stack of Content-Location URLs used for combining with a
           relative URL Content-Location on a leaf part in order to
           construct the full URL */
	struct _location_url_stack *urls;
	
	GHashTable *related;	/* related parts not displayed yet */
	
	/* Sigh.  This shouldn't be needed.  I haven't figured out why it is
	   though.  */
	GtkWidget *invisible;
	
	MailConfigDisplayStyle display_style;
	
	guint printing : 1;
};

typedef struct {
	GtkVBoxClass parent_class;
} MailDisplayClass;

GtkType        mail_display_get_type    (void);
GtkWidget *    mail_display_new         (void);

void           mail_display_initialize_gtkhtml (MailDisplay *mail_display, GtkHTML *html);

void           mail_display_queue_redisplay (MailDisplay *mail_display);
void           mail_display_render (MailDisplay *mail_display, GtkHTML *html, gboolean reset_scroll);
void           mail_display_redisplay (MailDisplay *mail_display, gboolean reset_scroll);
void           mail_display_redisplay_when_loaded (MailDisplay *md,
						   gconstpointer key,
						   void (*callback)(MailDisplay *, gpointer),
						   GtkHTML *html,
						   gpointer data);
void           mail_display_stream_write_when_loaded (MailDisplay *md,
						      gconstpointer key,
						      const gchar *url,
						      void (*callback)(MailDisplay *, gpointer),
						      GtkHTML *html,
						      GtkHTMLStream *handle,
						      gpointer data);

void           mail_display_set_message (MailDisplay *mail_display, 
					 CamelMedium *medium,
					 CamelFolder *folder,
					 CamelMessageInfo *info);

void           mail_display_set_charset (MailDisplay *mail_display,
					 const char *charset);

void           mail_display_load_images (MailDisplay *mail_display);


#define mail_html_write(html, stream, string) gtk_html_write (html, stream, string, strlen (string))

void           mail_text_write          (GtkHTML *html,
					 GtkHTMLStream *stream,
					 gboolean printing,
					 const char *text);
void           mail_error_printf        (GtkHTML *html,
					 GtkHTMLStream *stream,
					 const char *format, ...);

char *mail_display_add_url (MailDisplay *md, const char *kind, char *url, gpointer data);

const char *mail_display_get_url_for_icon (MailDisplay *md, const char *icon_name);

void mail_display_push_content_location (MailDisplay *md, const char *location);
CamelURL *mail_display_get_content_location (MailDisplay *md);
void mail_display_pop_content_location (MailDisplay *md);

#endif /* _MAIL_DISPLAY_H_ */
