/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Chris Toshok <toshok@ximian.com>
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

#include "eab-contact-display.h"

#include <string.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtkhtml/gtkhtml.h>
#include <gtkhtml/gtkhtml-stream.h>

#define PARENT_TYPE (gtk_vbox_get_type ())

struct _EABContactDisplayPrivate {
	GtkHTML *html;
	EContact *contact;
};


#define HTML_HEADER "<!doctype html public \"-//W3C//DTD HTML 4.0 TRANSITIONAL//EN\">\n<html>\n"  \
                    "<head>\n<meta name=\"generator\" content=\"Evolution Addressbook Component\">\n</head>\n"

static void
on_url_requested (GtkHTML *html, const char *url, GtkHTMLStream *handle,
		  gpointer user_data)
{
	EABContactDisplay *display = user_data;

	printf ("on_url_requested (%s)\n", url);
	if (!strcmp (url, "internal-contact-photo:")) {
		EContactPhoto *photo;

		photo = e_contact_get (display->priv->contact, E_CONTACT_PHOTO);

		printf ("writing a photo of length %d\n", photo->length);

		gtk_html_stream_write (handle, photo->data, photo->length);

		gtk_html_end (html, handle, GTK_HTML_STREAM_OK);
	}
}

static void
render_address (GtkHTMLStream *html_stream, EContact *contact, const char *html_label, EContactField adr_field, EContactField label_field)
{
	EContactAddress *adr;
	const char *label;

	label = e_contact_get_const (contact, label_field);
	if (label) {
		gtk_html_stream_printf (html_stream, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\"><tr valign=\"top\"><td>");
		gtk_html_stream_printf (html_stream, "<b>%s</b>:&nbsp;<td><pre>", html_label);
		gtk_html_stream_write (html_stream, label, strlen (label));
		gtk_html_stream_printf (html_stream, "</pre><br>");
		gtk_html_stream_printf (html_stream, "<a href=\"http://www.mapquest.com/\">Map It</a>");
		gtk_html_stream_printf (html_stream, "</td></tr></table>");
		return;
	}

	adr = e_contact_get (contact, adr_field);
	if (adr &&
	    (adr->po || adr->ext || adr->street || adr->locality || adr->region || adr->code || adr->country)) {

		gtk_html_stream_printf (html_stream, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\"><tr valign=\"top\"><td>");
		gtk_html_stream_printf (html_stream, "<b>%s</b>:&nbsp;<td>", html_label);

		if (adr->po && *adr->po) gtk_html_stream_printf (html_stream, "%s<br>", adr->po);
		if (adr->ext && *adr->ext) gtk_html_stream_printf (html_stream, "%s<br>", adr->ext);
		if (adr->street && *adr->street) gtk_html_stream_printf (html_stream, "%s<br>", adr->street);
		if (adr->locality && *adr->locality) gtk_html_stream_printf (html_stream, "%s<br>", adr->locality);
		if (adr->region && *adr->region) gtk_html_stream_printf (html_stream, "%s<br>", adr->region);
		if (adr->code && *adr->code) gtk_html_stream_printf (html_stream, "%s<br>", adr->code);
		if (adr->country && *adr->country) gtk_html_stream_printf (html_stream, "%s<br>", adr->country);

		gtk_html_stream_printf (html_stream, "<a href=\"http://www.mapquest.com/\">Map It</a>");
		gtk_html_stream_printf (html_stream, "</td></tr></table>");
	}
	if (adr)
		e_contact_address_free (adr);
}

void
eab_contact_display_render (EABContactDisplay *display, EContact *contact)
{
	GtkHTMLStream *html_stream;

	if (display->priv->contact)
		g_object_unref (display->priv->contact);
	display->priv->contact = contact;
	if (display->priv->contact)
		g_object_ref (display->priv->contact);

	html_stream = gtk_html_begin (display->priv->html);
	gtk_html_stream_write (html_stream, HTML_HEADER, sizeof (HTML_HEADER) - 1);
	gtk_html_stream_write (html_stream, "<body>\n", 7);

	/* XXX we need to entity-ize the strings we're passing to gtkhtml */

	if (contact) {
		char *str;
		EContactPhoto *photo;

		gtk_html_stream_printf (html_stream, "<table border=\"0\" valign=\"top\"><tr valign=\"top\"><td>");
		photo = e_contact_get (contact, E_CONTACT_PHOTO);
		if (photo) {
			gtk_html_stream_printf (html_stream, "<img src=\"internal-contact-photo:\">");
			e_contact_photo_free (photo);
		}
		
		gtk_html_stream_printf (html_stream, "</td><td>\n");

		str = e_contact_get_const (contact, E_CONTACT_FILE_AS);
		if (str)
			gtk_html_stream_printf (html_stream, "<h2>%s</h2>", str);
		else {
			str = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
			if (str)
				gtk_html_stream_printf (html_stream, "<h2>%s</h2>", str);
		}

		str = e_contact_get_const (contact, E_CONTACT_TITLE);
		if (str)
			gtk_html_stream_printf (html_stream, "<b>Job Title</b>: %s<br>", str);

		str = e_contact_get_const (contact, E_CONTACT_EMAIL_1);
		if (str)
			gtk_html_stream_printf (html_stream, "<b>Email</b>: %s<br>", str);
		str = e_contact_get_const (contact, E_CONTACT_EMAIL_2);
		if (str)
			gtk_html_stream_printf (html_stream, "<b>Email</b>: %s<br>", str);
		str = e_contact_get_const (contact, E_CONTACT_EMAIL_3);
		if (str)
			gtk_html_stream_printf (html_stream, "<b>Email</b>: %s<br>", str);


		render_address (html_stream, contact, "Home Address",  E_CONTACT_ADDRESS_HOME,  E_CONTACT_ADDRESS_LABEL_HOME);
		render_address (html_stream, contact, "Work Address",  E_CONTACT_ADDRESS_WORK,  E_CONTACT_ADDRESS_LABEL_WORK);
		render_address (html_stream, contact, "Other Address", E_CONTACT_ADDRESS_OTHER, E_CONTACT_ADDRESS_LABEL_OTHER);

		gtk_html_stream_printf (html_stream, "<hr>");

		str = e_contact_get_const (contact, E_CONTACT_HOMEPAGE_URL);
		if (str)
			gtk_html_stream_printf (html_stream, "<b>Home page</b>: <a href=\"%s\">%s</a><br>",
						str, str);

		str = e_contact_get_const (contact, E_CONTACT_BLOG_URL);
		if (str)
			gtk_html_stream_printf (html_stream, "<b>Blog</b>: <a href=\"%s\">%s</a><br>",
						str, str);

		gtk_html_stream_printf (html_stream, "</td></tr></table>\n");
	}

	gtk_html_stream_write (html_stream, "</body></html>\n", 15);
	gtk_html_end (display->priv->html, html_stream, GTK_HTML_STREAM_OK);
}

GtkWidget*
eab_contact_display_new (void)
{
	EABContactDisplay *display;
	GtkWidget *scroll;
	GtkHTML *html;

	display = g_object_new (E_TYPE_AB_CONTACT_DISPLAY, NULL);
	
	display->priv = g_new0 (EABContactDisplayPrivate, 1);

	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll), GTK_SHADOW_IN);
	gtk_box_pack_start_defaults (GTK_BOX (display), scroll);
	gtk_widget_show (scroll);

	html = display->priv->html = GTK_HTML (gtk_html_new ());

	gtk_html_set_default_content_type (html, "text/html; charset=utf-8");
	
	gtk_html_set_editable (html, FALSE);


	g_signal_connect (html, "url_requested",
			  G_CALLBACK (on_url_requested),
			  display);
#if 0
	g_signal_connect (html, "object_requested",
			  G_CALLBACK (on_object_requested),
			  mail_display);
	g_signal_connect (html, "link_clicked",
			  G_CALLBACK (on_link_clicked),
			  mail_display);
	g_signal_connect (html, "button_press_event",
			  G_CALLBACK (html_button_press_event), mail_display);
	g_signal_connect (html, "motion_notify_event",
			  G_CALLBACK (html_motion_notify_event), mail_display);
	g_signal_connect (html, "enter_notify_event",
			  G_CALLBACK (html_enter_notify_event), mail_display);
	g_signal_connect (html, "iframe_created",
			  G_CALLBACK (html_iframe_created), mail_display);
	g_signal_connect (html, "on_url",
			  G_CALLBACK (html_on_url), mail_display);
#endif

	gtk_container_add (GTK_CONTAINER (scroll), GTK_WIDGET (html));
	gtk_widget_show (GTK_WIDGET (html));

	return GTK_WIDGET (display);
}


static void
eab_contact_display_init (GObject *object)
{
}

static void
eab_contact_display_class_init (GtkObjectClass *object_class)
{
	//	object_class->destroy = mail_display_destroy;
}

GType
eab_contact_display_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info =  {
			sizeof (EABContactDisplayClass),
			NULL,           /* base_init */
			NULL,           /* base_finalize */
			(GClassInitFunc) eab_contact_display_class_init,
			NULL,           /* class_finalize */
			NULL,           /* class_data */
			sizeof (EABContactDisplay),
			0,             /* n_preallocs */
			(GInstanceInitFunc) eab_contact_display_init,
		};

		type = g_type_register_static (PARENT_TYPE, "EABContactDisplay", &info, 0);
	}

	return type;
}
