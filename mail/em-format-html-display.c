/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 *  Copyright 2003 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtkhtml/gtkhtml.h>
#include <gtkhtml/htmlengine.h>
#include <gtkhtml/htmlobject.h>
#include <gtkhtml/htmliframe.h>
#include <gtkhtml/htmlinterval.h>
#include <gtkhtml/gtkhtml-embedded.h>

#include <gtk/gtkvbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkarrow.h>

#include <gtk/gtkmenu.h>
#include <gtk/gtkmenuitem.h>

#include <libgnomevfs/gnome-vfs-mime-handlers.h>

#if 0
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <libgnomevfs/gnome-vfs-mime.h>
#endif

#include <bonobo/bonobo-control-frame.h>
#include <bonobo/bonobo-stream-memory.h>
#include <bonobo/bonobo-widget.h>

#include <camel/camel-stream.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-mime-filter-tohtml.h>
#include <camel/camel-mime-part.h>
#include <camel/camel-multipart.h>
#include <camel/camel-multipart-signed.h>
#include <camel/camel-internet-address.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-gpg-context.h>

#include <e-util/e-msgport.h>

#include "mail-config.h"

#include "em-format-html-display.h"
#include "em-marshal.h"
#include "e-searching-tokenizer.h"

#define d(x)

#define EFHD_TABLE_OPEN "<table>"

struct _EMFormatHTMLDisplayPrivate {
	int dummy;
};

static int efhd_html_button_press_event (GtkWidget *widget, GdkEventButton *event, EMFormatHTMLDisplay *efh);
static void efhd_html_link_clicked (GtkHTML *html, const char *url, EMFormatHTMLDisplay *efhd);

struct _attach_puri {
	EMFormatPURI puri;

	const EMFormatHandler *handle;

	/* for the > and V buttons */
	GtkWidget *forward, *down;
	/* currently no way to correlate this data to the frame :( */
	GtkHTML *frame;
	CamelStream *output;
	unsigned int shown:1;
};

static void efhd_iframe_created(GtkHTML *html, GtkHTML *iframe, EMFormatHTMLDisplay *efh);
/*static void efhd_url_requested(GtkHTML *html, const char *url, GtkHTMLStream *handle, EMFormatHTMLDisplay *efh);
  static gboolean efhd_object_requested(GtkHTML *html, GtkHTMLEmbedded *eb, EMFormatHTMLDisplay *efh);*/

static void efhd_format_clone(EMFormat *, CamelMedium *, EMFormat *);
static void efhd_format_error(EMFormat *emf, CamelStream *stream, const char *txt);
static void efhd_format_message(EMFormat *, CamelStream *, CamelMedium *);
static void efhd_format_source(EMFormat *, CamelStream *, CamelMimePart *);
static void efhd_format_attachment(EMFormat *, CamelStream *, CamelMimePart *, const char *, const EMFormatHandler *);

static void efhd_builtin_init(EMFormatHTMLDisplayClass *efhc);

enum {
	EFHD_LINK_CLICKED,
	EFHD_POPUP_EVENT,
	EFHD_LAST_SIGNAL,
};

static guint efhd_signals[EFHD_LAST_SIGNAL] = { 0 };


static EMFormatHTMLClass *efhd_parent;

static void
efhd_init(GObject *o)
{
	EMFormatHTMLDisplay *efhd = (EMFormatHTMLDisplay *)o;
	GtkStyle *style;
#define efh ((EMFormatHTML *)efhd)

	efhd->priv = g_malloc0(sizeof(*efhd->priv));

	efhd->search_tok = (ESearchingTokenizer *)e_searching_tokenizer_new();
	html_engine_set_tokenizer(efh->html->engine, (HTMLTokenizer *)efhd->search_tok);

	/* we want to convert url's etc */
	efh->text_html_flags |= CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS | CAMEL_MIME_FILTER_TOHTML_CONVERT_ADDRESSES;

	/* FIXME: does this have to be re-done every time we draw? */

	/* My favorite thing to do... muck around with colors so we respect people's stupid themes.
	   However, we only do this if we are rendering to the screen -- we ignore the theme
	   when we are printing. */
	style = gtk_widget_get_style((GtkWidget *)efh->html);
	if (style) {
		int state = GTK_WIDGET_STATE((GtkWidget *)efh->html);
		gushort r, g, b;
#define SCALE (238)

		/* choose a suitably darker or lighter colour */
		r = style->base[state].red >> 8;
		g = style->base[state].green >> 8;
		b = style->base[state].blue >> 8;

		if (r+b+g > 128*3) {
			r = (r*SCALE) >> 8;
			g = (g*SCALE) >> 8;
			b = (b*SCALE) >> 8;
		} else {
			r = (255 - (SCALE * (255-r))) >> 8;
			g = (255 - (SCALE * (255-g))) >> 8;
			b = (255 - (SCALE * (255-b))) >> 8;
		}

		efh->header_colour = ((r<<16) | (g<< 8) | b) & 0xffffff;
		
		r = style->text[state].red >> 8;
		g = style->text[state].green >> 8;
		b = style->text[state].blue >> 8;

		efh->text_colour = ((r<<16) | (g<< 8) | b) & 0xffffff;
	}
#undef SCALE
#undef efh
}

static void
efhd_finalise(GObject *o)
{
	EMFormatHTMLDisplay *efhd = (EMFormatHTMLDisplay *)o;

	/* check pending stuff */

	g_free(efhd->priv);

	((GObjectClass *)efhd_parent)->finalize(o);
}

static gboolean
efhd_bool_accumulator(GSignalInvocationHint *ihint, GValue *out, const GValue *in, void *data)
{
	gboolean val = g_value_get_boolean(in);

	g_value_set_boolean(out, val);

	return !val;
}

static void
efhd_class_init(GObjectClass *klass)
{
	((EMFormatClass *)klass)->format_clone = efhd_format_clone;
	((EMFormatClass *)klass)->format_error = efhd_format_error;
	((EMFormatClass *)klass)->format_message = efhd_format_message;
	((EMFormatClass *)klass)->format_source = efhd_format_source;
	((EMFormatClass *)klass)->format_attachment = efhd_format_attachment;

	klass->finalize = efhd_finalise;

	efhd_signals[EFHD_LINK_CLICKED] = 
		g_signal_new("link_clicked",
			     G_TYPE_FROM_CLASS(klass),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(EMFormatHTMLDisplayClass, link_clicked),
			     NULL, NULL,
			     g_cclosure_marshal_VOID__POINTER,
			     G_TYPE_NONE, 1, G_TYPE_POINTER);

	efhd_signals[EFHD_POPUP_EVENT] = 
		g_signal_new("popup_event",
			     G_TYPE_FROM_CLASS(klass),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(EMFormatHTMLDisplayClass, popup_event),
			     efhd_bool_accumulator, NULL,
			     em_marshal_BOOLEAN__BOXED_POINTER_POINTER,
			     G_TYPE_BOOLEAN, 3,
			     GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE,
			     G_TYPE_POINTER, G_TYPE_POINTER);

	efhd_builtin_init((EMFormatHTMLDisplayClass *)klass);
}

GType
em_format_html_display_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMFormatHTMLDisplayClass),
			NULL, NULL,
			(GClassInitFunc)efhd_class_init,
			NULL, NULL,
			sizeof(EMFormatHTMLDisplay), 0,
			(GInstanceInitFunc)efhd_init
		};
		efhd_parent = g_type_class_ref(em_format_html_get_type());
		type = g_type_register_static(em_format_html_get_type(), "EMFormatHTMLDisplay", &info, 0);
	}

	return type;
}

EMFormatHTMLDisplay *em_format_html_display_new(void)
{
	EMFormatHTMLDisplay *efhd;

	efhd = g_object_new(em_format_html_display_get_type(), 0);

	g_signal_connect(efhd->formathtml.html, "iframe_created", G_CALLBACK(efhd_iframe_created), efhd);
	g_signal_connect(efhd->formathtml.html, "link_clicked", G_CALLBACK(efhd_html_link_clicked), efhd);
	g_signal_connect(efhd->formathtml.html, "button_press_event", G_CALLBACK(efhd_html_button_press_event), efhd);

	return efhd;
}

void em_format_html_display_goto_anchor(EMFormatHTMLDisplay *efhd, const char *name)
{
	printf("FIXME: go to anchor '%s'\n", name);
}

void
em_format_html_display_set_search(EMFormatHTMLDisplay *efhd, int type, GSList *strings)
{
	efhd->search_matches = 0;

	switch(type&3) {
	case EM_FORMAT_HTML_DISPLAY_SEARCH_PRIMARY:
		e_searching_tokenizer_set_primary_case_sensitivity(efhd->search_tok, (type&EM_FORMAT_HTML_DISPLAY_SEARCH_ICASE) == 0);
		e_searching_tokenizer_set_primary_search_string(efhd->search_tok, NULL);
		while (strings) {
			e_searching_tokenizer_add_primary_search_string(efhd->search_tok, strings->data);
			strings = strings->next;
		}
		break;
	case EM_FORMAT_HTML_DISPLAY_SEARCH_SECONDARY:
	default:
		e_searching_tokenizer_set_secondary_case_sensitivity(efhd->search_tok, (type&EM_FORMAT_HTML_DISPLAY_SEARCH_ICASE) == 0);
		e_searching_tokenizer_set_secondary_search_string(efhd->search_tok, NULL);
		while (strings) {
			e_searching_tokenizer_add_secondary_search_string(efhd->search_tok, strings->data);
			strings = strings->next;
		}
		break;
	}

	d(printf("redrawing with search\n"));
	em_format_format_clone((EMFormat *)efhd, ((EMFormat *)efhd)->message, (EMFormat *)efhd);
}


void
em_format_html_display_zoom_in (EMFormatHTMLDisplay *efhd)
{
	gtk_html_zoom_in (((EMFormatHTML *) efhd)->html);
}

void
em_format_html_display_zoom_out (EMFormatHTMLDisplay *efhd)
{
	gtk_html_zoom_out (((EMFormatHTML *) efhd)->html);
}

void
em_format_html_display_zoom_reset (EMFormatHTMLDisplay *efhd)
{
	gtk_html_zoom_reset (((EMFormatHTML *) efhd)->html);
}

/* ********************************************************************** */

static void
efhd_iframe_created(GtkHTML *html, GtkHTML *iframe, EMFormatHTMLDisplay *efh)
{
	d(printf("Iframe created %p ... \n", iframe));

	g_signal_connect(iframe, "button_press_event", G_CALLBACK (efhd_html_button_press_event), efh);

	return;
}

static int
efhd_html_button_press_event (GtkWidget *widget, GdkEventButton *event, EMFormatHTMLDisplay *efhd)
{
	HTMLEngine *e;
	HTMLPoint *point;
	const char *url;
	gboolean res = FALSE;

	if (event->button != 3)
		return FALSE;

	e = ((GtkHTML *)widget)->engine;
	point = html_engine_get_point_at(e, event->x, event->y, FALSE);			
	if (point == NULL)
		return FALSE;

	d(printf("popup button pressed\n"));

	if ( (url = html_object_get_src(point->object)) != NULL
	     || (url = html_object_get_url(point->object)) != NULL) {
		EMFormatPURI *puri;
		char *uri;

		uri = gtk_html_get_url_object_relative((GtkHTML *)widget, point->object, url);
		puri = em_format_find_puri((EMFormat *)efhd, uri);

		d(printf("poup event, uri = '%s' part = '%p'\n", uri, puri?puri->part:NULL));

		g_signal_emit((GtkObject *)efhd, efhd_signals[EFHD_POPUP_EVENT], 0, event, uri, puri?puri->part:NULL, &res);
		g_free(uri);
	}
	
	html_point_destroy(point);

	return res;
}

static void
efhd_html_link_clicked (GtkHTML *html, const char *url, EMFormatHTMLDisplay *efhd)
{
	d(printf("link clicked event '%s'\n", url));
	g_signal_emit((GObject *)efhd, efhd_signals[EFHD_LINK_CLICKED], 0, url);
}

/* ********************************************************************** */

static void
efhd_signature_check(GtkWidget *w, EMFormatHTMLPObject *pobject)
{
	d(printf("insert signature check here ... redraw ?  or what ?\n"));
	/* blah, do the old way for now, force a complete re-draw */
	em_format_set_inline((EMFormat *)pobject->format, pobject->part, TRUE);
	em_format_format_clone((EMFormat *)pobject->format, ((EMFormat *)pobject->format)->message, (EMFormat *)pobject->format);
}

static gboolean
efhd_signature_button(EMFormatHTML *efh, GtkHTMLEmbedded *eb, EMFormatHTMLPObject *pobject)
{
	GtkWidget *icon, *button;
	GdkPixbuf *pixbuf;

	pixbuf = gdk_pixbuf_new_from_file(EVOLUTION_ICONSDIR "/pgp-signature-nokey.png", NULL);
	if (pixbuf == NULL)
		return FALSE;

	/* wtf isn't this just scaled on disk? */
	icon = gtk_image_new_from_pixbuf(gdk_pixbuf_scale_simple(pixbuf, 24, 24, GDK_INTERP_BILINEAR));
	g_object_unref(pixbuf);
	gtk_widget_show(icon);

	button = gtk_button_new();
	g_signal_connect(button, "clicked", G_CALLBACK (efhd_signature_check), pobject);
	/*g_signal_connect (button, "key_press_event", G_CALLBACK (inline_button_press), part);*/

	gtk_container_add((GtkContainer *)button, icon);
	gtk_widget_show(button);
	gtk_container_add((GtkContainer *)eb, button);

	return TRUE;
}

static void
efhd_multipart_signed (EMFormat *emf, CamelStream *stream, CamelMimePart *part, const EMFormatHandler *info)
{
	char *classid;
	static int signedid;
	CamelMultipartSigned *mps;
	CamelMimePart *cpart;

	mps = (CamelMultipartSigned *)camel_medium_get_content_object((CamelMedium *)part);
	if (!CAMEL_IS_MULTIPART_SIGNED(mps)
	    || (cpart = camel_multipart_get_part((CamelMultipart *)mps, CAMEL_MULTIPART_SIGNED_CONTENT)) == NULL) {
		em_format_format_source(emf, stream, part);
		return;
	}

	em_format_part(emf, stream, cpart);

	if (em_format_is_inline(emf, part)) {
		camel_stream_printf(stream, "inlined signature ...<br>");
		em_format_html_multipart_signed_sign(emf, stream, part);
	} else {
		classid = g_strdup_printf("multipart-signed:///icon/%d", signedid++);

		/* wtf is this so fugly? */
		camel_stream_printf(stream,
				    "<br><table cellspacing=0 cellpadding=0>"
				    "<tr><td><table width=10 cellspacing=0 cellpadding=0>"
				    "<tr><td></td></tr></table></td>"
				    "<td><object classid=\"%s\"></object></td>"
				    "<td><table width=3 cellspacing=0 cellpadding=0>"
				    "<tr><td></td></tr></table></td>"
				    "<td><font size=-1>%s</font></td></tr>"
				    "<tr><td height=10>"
				    "<table cellspacing=0 cellpadding=0><tr>"
				    "<td height=10><a name=\"glue\"></td></tr>"
				    "</table></td></tr></table>\n",
				    classid,
				    _("This message is digitally signed. Click the lock icon for more information."));

		em_format_html_add_pobject((EMFormatHTML *)emf, classid, efhd_signature_button, part);
		g_free(classid);
	}
}

/* ********************************************************************** */

static EMFormatHandler type_builtin_table[] = {
	{ "multipart/signed", (EMFormatFunc)efhd_multipart_signed },
};

static void
efhd_builtin_init(EMFormatHTMLDisplayClass *efhc)
{
	int i;

	for (i=0;i<sizeof(type_builtin_table)/sizeof(type_builtin_table[0]);i++)
		em_format_class_add_handler((EMFormatClass *)efhc, &type_builtin_table[i]);
}

/* ********************************************************************** */

static void efhd_format_clone(EMFormat *emf, CamelMedium *part, EMFormat *src)
{
	((EMFormatClass *)efhd_parent)->format_clone(emf, part, src);

	((EMFormatHTMLDisplay *)emf)->search_matches = e_searching_tokenizer_match_count(((EMFormatHTMLDisplay *)emf)->search_tok);
}

/* TODO: if these aren't going to do anything should remove */
static void efhd_format_error(EMFormat *emf, CamelStream *stream, const char *txt)
{
	((EMFormatClass *)efhd_parent)->format_error(emf, stream, txt);
}

static void efhd_format_message(EMFormat *emf, CamelStream *stream, CamelMedium *part)
{
	((EMFormatClass *)efhd_parent)->format_message(emf, stream, part);
}

static void efhd_format_source(EMFormat *emf, CamelStream *stream, CamelMimePart *part)
{
	((EMFormatClass *)efhd_parent)->format_source(emf, stream, part);
}

/* ********************************************************************** */

/* if it hasn't been processed yet, format the attachment */
static void
efhd_attachment_show(GtkWidget *w, struct _attach_puri *info)
{
	d(printf("show attachment button called\n"));

	info->shown = ~info->shown;
	em_format_set_inline(info->puri.format, info->puri.part, info->shown);
	/* FIXME: do this in an idle handler */
	em_format_format_clone(info->puri.format, info->puri.format->message, info->puri.format);
#if 0
	/* FIXME: track shown state in parent */

	if (info->shown) {
		d(printf("hiding\n"));
		info->shown = FALSE;
		if (info->frame)
			gtk_widget_hide((GtkWidget *)info->frame);
		gtk_widget_show(info->forward);
		gtk_widget_hide(info->down);
	} else {
		d(printf("showing\n"));
		info->shown = TRUE;
		if (info->frame)
			gtk_widget_show((GtkWidget *)info->frame);
		gtk_widget_hide(info->forward);
		gtk_widget_show(info->down);

		/* have we decoded it yet? */
		if (info->output) {
			info->handle->handler(info->puri.format, info->output, info->puri.part, info->handle);
			camel_stream_close(info->output);
			camel_object_unref(info->output);
			info->output = NULL;
		}
	}

	em_format_set_inline(info->puri.format, info->puri.part, info->shown);
#endif
}

static gboolean
efhd_attachment_popup(GtkWidget *w, GdkEventButton *event, struct _attach_puri *info)
{
	GtkMenu *menu;
	GtkWidget *item;

	/* FIXME FIXME
	   How can i do this with plugins!?
	   extension point=com.ximian.evolution.mail.attachmentPopup?? */

	d(printf("attachment popup, button %d\n", event->button));

	if (event->button != 1 && event->button != 3) {
		/* ?? gtk_propagate_event(GTK_WIDGET (user_data), (GdkEvent *)event);*/
		return FALSE;
	}

	menu = (GtkMenu *)gtk_menu_new();
	item = gtk_menu_item_new_with_mnemonic(_("Save Attachment..."));
	gtk_menu_shell_append((GtkMenuShell *)menu, item);

	/* FIXME: bonobo component handlers? */
	if (info->handle) {
		GList *apps;

		if (info->shown) {
			item = gtk_menu_item_new_with_mnemonic(_("Hide"));
		} else {
			item = gtk_menu_item_new_with_mnemonic(_("View Inline"));
		}
		g_signal_connect(item, "activate", G_CALLBACK(efhd_attachment_show), info);
		gtk_menu_shell_append((GtkMenuShell *)menu, item);

		apps = gnome_vfs_mime_get_short_list_applications(info->handle->mime_type);
		if (apps) {
			GList *l = apps;
			GString *label = g_string_new("");

			while (l) {
				GnomeVFSMimeApplication *app = l->data;
			
				g_string_printf(label, _("Open in %s..."), app->name);
				item = gtk_menu_item_new_with_label(label->str);
				gtk_menu_shell_append((GtkMenuShell *)menu, item);
				l = l->next;
			}
			g_string_free(label, TRUE);
			g_list_free(apps);
		}
	}

	gtk_widget_show_all((GtkWidget *)menu);
	g_signal_connect(menu, "selection_done", G_CALLBACK(gtk_widget_destroy), menu);
	gtk_menu_popup(menu, NULL, NULL, NULL, NULL, event->button, event->time);

	return TRUE;
}

/* attachment button callback */
static gboolean
efhd_attachment_button(EMFormatHTML *efh, GtkHTMLEmbedded *eb, EMFormatHTMLPObject *pobject)
{
	struct _attach_puri *info;
	GtkWidget *hbox, *w, *button, *mainbox;

	/* FIXME: handle default shown case */
	d(printf("adding attachment button/content\n"));

	info = (struct _attach_puri *)em_format_find_puri((EMFormat *)efh, pobject->classid);
	g_assert(info != NULL);
	g_assert(info->forward == NULL);

	mainbox = gtk_hbox_new(FALSE, 0);

	button = gtk_button_new();
	GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);

	if (info->handle)
		g_signal_connect(button, "clicked", G_CALLBACK(efhd_attachment_show), info);
	else
		gtk_widget_set_sensitive(button, FALSE);

	hbox = gtk_hbox_new(FALSE, 2);
	info->forward = gtk_image_new_from_stock(GTK_STOCK_GO_FORWARD, GTK_ICON_SIZE_BUTTON);
	gtk_box_pack_start((GtkBox *)hbox, info->forward, TRUE, TRUE, 0);
	if (info->handle) {
		info->down = gtk_image_new_from_stock(GTK_STOCK_GO_DOWN, GTK_ICON_SIZE_BUTTON);
		gtk_box_pack_start((GtkBox *)hbox, info->down, TRUE, TRUE, 0);
	}
	/* FIXME: Pixmap loader */
	w = gtk_image_new();
	gtk_widget_set_size_request(w, 24, 24);
	gtk_box_pack_start((GtkBox *)hbox, w, TRUE, TRUE, 0);
	gtk_container_add((GtkContainer *)button, hbox);
	gtk_box_pack_start((GtkBox *)mainbox, button, TRUE, TRUE, 0);

	button = gtk_button_new();
	GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);
	gtk_container_add((GtkContainer *)button, gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_ETCHED_IN));
	g_signal_connect(button, "button_press_event", G_CALLBACK(efhd_attachment_popup), info);
	gtk_box_pack_start((GtkBox *)mainbox, button, TRUE, TRUE, 0);

	gtk_widget_show_all(mainbox);

	if (info->shown)
		gtk_widget_hide(info->forward);
	else
		gtk_widget_hide(info->down);

	gtk_container_add((GtkContainer *)eb, mainbox);

	return TRUE;
}

/* not used currently */
/* frame source callback */
static void
efhd_attachment_frame(EMFormat *emf, CamelStream *stream, EMFormatPURI *puri)
{
	struct _attach_puri *info = (struct _attach_puri *)puri;

	if (info->shown) {
		d(printf("writing to frame content, handler is '%s'\n", info->handle->mime_type));
		info->handle->handler(emf, stream, info->puri.part, info->handle);
		camel_stream_close(stream);
	} else {
		/* FIXME: this is leaked if the object is closed without showing it
		   NB: need a virtual puri_free method? */
		info->output = stream;
		camel_object_ref(stream);
	}
}

static gboolean
efhd_bonobo_object(EMFormatHTML *efh, GtkHTMLEmbedded *eb, EMFormatHTMLPObject *pobject)
{
	CamelDataWrapper *wrapper;
	Bonobo_ServerInfo *component;
	GtkWidget *embedded;
	Bonobo_PersistStream persist;	
	CORBA_Environment ev;
	CamelStreamMem *cstream;
	BonoboStream *bstream;
	BonoboControlFrame *control_frame;
	Bonobo_PropertyBag prop_bag;

	component = gnome_vfs_mime_get_default_component(eb->type);
	if (component == NULL)
		return FALSE;

	embedded = bonobo_widget_new_control(component->iid, NULL);
	CORBA_free(component);
	if (embedded == NULL)
		return FALSE;
	
	CORBA_exception_init(&ev);

	control_frame = bonobo_widget_get_control_frame((BonoboWidget *)embedded);
	prop_bag = bonobo_control_frame_get_control_property_bag(control_frame, NULL);
	if (prop_bag != CORBA_OBJECT_NIL) {
		/*
		 * Now we can take care of business. Currently, the only control
		 * that needs something passed to it through a property bag is
		 * the iTip control, and it needs only the From email address,
		 * but perhaps in the future we can generalize this section of code
		 * to pass a bunch of useful things to all embedded controls.
		 */
		const CamelInternetAddress *from;
		char *from_address;
				
		from = camel_mime_message_get_from((CamelMimeMessage *)((EMFormat *)efh)->message);
		from_address = camel_address_encode((CamelAddress *)from);
		bonobo_property_bag_client_set_value_string(prop_bag, "from_address", from_address, &ev);
		g_free(from_address);
		
		Bonobo_Unknown_unref(prop_bag, &ev);
	}
	
	persist = (Bonobo_PersistStream)Bonobo_Unknown_queryInterface(bonobo_widget_get_objref((BonoboWidget *)embedded),
								      "IDL:Bonobo/PersistStream:1.0", NULL);
	if (persist == CORBA_OBJECT_NIL) {
		gtk_object_sink((GtkObject *)embedded);
		CORBA_exception_free(&ev);				
		return FALSE;
	}
	
	/* Write the data to a CamelStreamMem... */
	cstream = (CamelStreamMem *)camel_stream_mem_new();
	wrapper = camel_medium_get_content_object((CamelMedium *)pobject->part);
 	camel_data_wrapper_decode_to_stream(wrapper, (CamelStream *)cstream);
	
	/* ...convert the CamelStreamMem to a BonoboStreamMem... */
	bstream = bonobo_stream_mem_create(cstream->buffer->data, cstream->buffer->len, TRUE, FALSE);
	camel_object_unref(cstream);
	
	/* ...and hydrate the PersistStream from the BonoboStream. */
	Bonobo_PersistStream_load(persist,
				  bonobo_object_corba_objref(BONOBO_OBJECT (bstream)),
				  eb->type, &ev);
	bonobo_object_unref(BONOBO_OBJECT (bstream));
	Bonobo_Unknown_unref(persist, &ev);
	CORBA_Object_release(persist, &ev);
	
	if (ev._major != CORBA_NO_EXCEPTION) {
		gtk_object_sink((GtkObject *)embedded);
		CORBA_exception_free(&ev);				
		return FALSE;
	}
	CORBA_exception_free(&ev);
	
	gtk_widget_show(embedded);
	gtk_container_add(GTK_CONTAINER (eb), embedded);
	
	return TRUE;
}

static gboolean
efhd_check_server_prop(Bonobo_ServerInfo *component, const char *propname, const char *value)
{
	CORBA_sequence_CORBA_string stringv;
	Bonobo_ActivationProperty *prop;
	int i;

	prop = bonobo_server_info_prop_find(component, propname);
	if (!prop || prop->v._d != Bonobo_ACTIVATION_P_STRINGV)
		return FALSE;

	stringv = prop->v._u.value_stringv;
	for (i = 0; i < stringv._length; i++) {
		if (!g_ascii_strcasecmp(value, stringv._buffer[i]))
			return TRUE;
	}

	return FALSE;
}

static gboolean
efhd_use_component(const char *mime_type)
{
	GList *components, *iter;
	Bonobo_ServerInfo *component = NULL;

	/* should this cache it? */

	if (g_ascii_strcasecmp(mime_type, "text/x-vcard") != 0
	    && g_ascii_strcasecmp(mime_type, "text/calendar") != 0) {
		const char **mime_types;
		int i;

		mime_types = mail_config_get_allowable_mime_types();
		for (i = 0; mime_types[i]; i++) {
			if (!g_ascii_strcasecmp(mime_types[i], mime_type))
				goto type_ok;
		}
		return FALSE;
	}
type_ok:
	components = gnome_vfs_mime_get_all_components (mime_type);
	for (iter = components; iter; iter = iter->next) {
		Bonobo_ServerInfo *comp = iter->data;

		comp = iter->data;
		if (efhd_check_server_prop(comp, "repo_ids", "IDL:Bonobo/PersistStream:1.0")
		    && efhd_check_server_prop(comp, "bonobo:supported_mime_types", mime_type)) {
			component = comp;
			break;
		}
	}
	gnome_vfs_mime_component_list_free (components);

	return component != NULL;
}

static void
efhd_format_attachment(EMFormat *emf, CamelStream *stream, CamelMimePart *part, const char *mime_type, const EMFormatHandler *handle)
{
	char *classid, *text, *html;
	struct _attach_puri *info;

	classid = g_strdup_printf("attachment-%p", part);
	info = (struct _attach_puri *)em_format_add_puri(emf, sizeof(*info), classid, part, efhd_attachment_frame);
	em_format_html_add_pobject((EMFormatHTML *)emf, classid, efhd_attachment_button, part);
	info->handle = handle;
	info->shown = em_format_is_inline(emf, info->puri.part) && handle != NULL;

	camel_stream_write_string(stream,
				  "<table cellspacing=0 cellpadding=0><tr><td>"
				  "<table width=10 cellspacing=0 cellpadding=0>"
				  "<tr><td></td></tr></table></td>");

	camel_stream_printf(stream, "<td><object classid=\"%s\"></object></td>", classid);

	camel_stream_write_string(stream,
				  "<td><table width=3 cellspacing=0 cellpadding=0>"
				  "<tr><td></td></tr></table></td><td><font size=-1>");

	/* output some info about it */
	/* FIXME: should we look up mime_type from object again? */
	text = em_format_describe_part(part, mime_type);
	html = camel_text_to_html(text, ((EMFormatHTML *)emf)->text_html_flags & CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS, 0);
	camel_stream_write_string(stream, html);
	g_free(html);
	g_free(text);

	camel_stream_write_string(stream, "</font></td></tr><tr></table>");

	if (handle) {
		printf("adding attachment '%s' content, type is '%s'\n", classid, mime_type);
		if (info->shown)
			handle->handler(emf, stream, part, handle);
		/*camel_stream_printf(stream, "<iframe src=\"%s\" marginheight=0 marginwidth=0>%s</iframe>\n", classid, _("Attachment content could not be loaded"));*/
	} else if (efhd_use_component(mime_type)) {
		static int partid;

		g_free(classid); /* messy */

		classid = g_strdup_printf("bonobo-unknown:///em-formath-html-display/%p/%d", part, partid++);
		em_format_html_add_pobject((EMFormatHTML *)emf, classid, efhd_bonobo_object, part);
		camel_stream_printf(stream, "<object classid=\"%s\" type=\"%s\">\n", classid, mime_type);
	}

	g_free(classid);
}
