/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-summary-rdf.c
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Iain Holmes
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <glib.h>
#include <gtk/gtkmain.h>

#include <gnome-xml/parser.h>
#include <gnome-xml/xmlmemory.h>

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <gal/widgets/e-unicode.h>
#include <libgnomevfs/gnome-vfs.h>
#include "e-summary.h"

struct _ESummaryRDF {
	ESummaryConnection *connection;
	GList *rdfs;

	char *html;
	guint32 timeout;
	gboolean online;
};

typedef struct _RDF {
	char *uri;
	char *html;
	GnomeVFSAsyncHandle *handle;
	GString *string;
	char *buffer;

	xmlDocPtr cache;
	ESummary *summary;

	gboolean shown;
} RDF;

int xmlSubstituteEntitiesDefaultValue = 1;

char *
e_summary_rdf_get_html (ESummary *summary)
{
	GList *rdfs;
	char *html;
	GString *string;

	if (summary->rdf == NULL) {
		return NULL;
	}

	string = g_string_new ("");
	for (rdfs = summary->rdf->rdfs; rdfs; rdfs = rdfs->next) {
		if (((RDF *)rdfs->data)->html == NULL) {
			continue;
		}

		g_string_append (string, ((RDF *)rdfs->data)->html);
	}

	html = string->str;
	g_string_free (string, FALSE);
	return html;
}

/************ RDF Parser *******************/

static char *
layer_find (xmlNodePtr node, 
	    char *match, 
	    char *fail)
{
	while (node!=NULL) {
#ifdef RDF_DEBUG
		xmlDebugDumpNode (stdout, node, 32);
		printf("%s.\n", node->name);
#endif
		if (strcasecmp (node->name, match)==0) {
			if (node->childs != NULL && node->childs->content != NULL) {
				return node->childs->content;
			} else {
				return fail;
			}
		}
		node = node->next;
	}
	return fail;
}

static char *
layer_find_url (xmlNodePtr node, 
		char *match, 
		char *fail)
{
	char *p = layer_find (node, match, fail);
	char *r = p;
	static char *wb = NULL;
	char *w;
	
	if (wb) {
		g_free (wb);
	}
	
	wb = w = g_malloc (3 * strlen (p));
	
	if (*r == ' ') r++;	/* Fix UF bug */

	while (*r) {
		if (strncmp (r, "&amp;", 5) == 0) {
			*w++ = '&';
			r += 5;
			continue;
		}
		if (strncmp (r, "&lt;", 4) == 0) {
			*w++ = '<';
			r += 4;
			continue;
		}
		if (strncmp (r, "&gt;", 4) == 0) {
			*w++ = '>';
			r += 4;
			continue;
		}
		if (*r == '"' || *r == ' '){
			*w++ = '%';
			*w++ = "0123456789ABCDEF"[*r/16];
			*w++ = "0123456789ABCDEF"[*r&15];
			r++;
			continue;
		}
		*w++ = *r++;
	}
	*w = 0;
	return wb;
}

static void 
tree_walk (xmlNodePtr root,
	   RDF *r,
	   GString *html)
{
	xmlNodePtr walk;
	xmlNodePtr rewalk = root;
	xmlNodePtr channel = NULL;
	xmlNodePtr image = NULL;
	xmlNodePtr item[16];
	int items = 0;
	int limit;
	int i;
	char *t, *u;
	char *tmp;

	if (r->summary->preferences == NULL) {
		limit = 10;
	} else {
		limit = r->summary->preferences->limit;
	}

	/* FIXME: Need arrows */
	if (r->shown == FALSE) {
		char *p;

		/* FIXME: Hash table & UID */
		p = g_strdup_printf ("<font size=\"-2\"><a href=\"rdf://%d\">(+)</a></font>", GPOINTER_TO_INT (r));
		g_string_append (html, p);
		g_free (p);
	} else {
		char *p;

		/* FIXME: Hash table & UID */
		p = g_strdup_printf ("<font size=\"-2\"><a href=\"rdf://%d\">(-)</a></font>", GPOINTER_TO_INT (r));
		g_string_append (html, p);
		g_free (p);
	}
	
	do {
		walk = rewalk;
		rewalk = NULL;
		
		while (walk!=NULL){
#ifdef RDF_DEBUG
			printf ("%p, %s\n", walk, walk->name);
#endif
			if (strcasecmp (walk->name, "rdf") == 0) {
				rewalk = walk->childs;
				walk = walk->next;
				continue;
			}
			if (strcasecmp (walk->name, "rss") == 0){
				rewalk = walk->childs;
				walk = walk->next;
				continue;
			}
			/* This is the channel top level */
#ifdef RDF_DEBUG
			printf ("Top level '%s'.\n", walk->name);
#endif
			if (strcasecmp (walk->name, "channel") == 0) {
				channel = walk;
				rewalk = channel->childs;
			}
			if (strcasecmp (walk->name, "image") == 0) {
				image = walk;
				g_print ("Image\n");
			}
			if (strcasecmp (walk->name, "item") == 0 && items < 16) {
				item[items++] = walk;
			}
			walk = walk->next;
		}
	}
	while (rewalk);
	
	if (channel == NULL) {
		fprintf(stderr, "No channel definition.\n");
		return;
	}

	t = layer_find(channel->childs, "title", "");
	u = layer_find(channel->childs, "link", "");

	if (*u != '\0')
		g_string_sprintfa (html, "<a href=\"%s\">", u);
	if (r->cache->encoding)
		t = e_utf8_from_charset_string (r->cache->encoding, t);
	else
		t = e_utf8_from_locale_string (t);
	g_string_append (html, t);
	g_free (t);
	if (*u != '\0') {
		g_string_append (html, "</a>");
	}
	g_string_append (html, "</b></dt>");

	if (r->shown == FALSE) {
		g_string_append (html, "</dl>");
		return;
	}

	g_string_append (html, "<ul>");

	items = MIN (limit, items);
	for (i = 0; i < items; i++) {
		char *p = layer_find (item[i]->childs, "title", "No information");
		
		tmp = g_strdup_printf ("<LI><font size=\"-1\"><A href=\"%s\">\n", layer_find_url(item[i]->childs, "link", ""));
		g_string_append (html, tmp);
		g_free (tmp);
		
		if (r->cache->encoding)
			p = e_utf8_from_charset_string (r->cache->encoding, p);
		else
			p = e_utf8_from_locale_string (p);
		tmp = g_strdup_printf ("%s\n</A></font></li>", p);
		g_free (p);
		g_string_append (html, tmp);
		g_free (tmp);
	}
	g_string_append (html, "</UL>");
}

static void
display_doc (RDF *r)
{
	GString *html;

	html = g_string_new ("<dl><dt><img src=\"ico-rdf.png\" align=\"middle\" "
			     "width=\"48\" height=\"48\">");

	if (r->cache == NULL) {
		char *tmp_utf, *str;

		str = g_strdup_printf ("<b>%s:</b><br>%s", _("Error downloading RDF"),
				       r->uri);
		tmp_utf = e_utf8_from_locale_string (str);
		g_free (str);

		g_string_append (html, tmp_utf);
		g_string_append (html, "</dt>");
		g_free (tmp_utf);
	} else {
		tree_walk (r->cache->root, r, html);
	}

	g_free (r->html);
	g_string_append (html, "</dl>");
	r->html = html->str;
	g_string_free (html, FALSE);

	e_summary_draw (r->summary);
}

static void
close_callback (GnomeVFSAsyncHandle *handle,
		GnomeVFSResult result,
		RDF *r)
{
	ESummary *summary;
	char *xml;
	xmlDocPtr doc;

	summary = r->summary;
	if (summary->rdf->connection->callback) {
		ESummaryConnection *connection = summary->rdf->connection;
		connection->callback (summary, connection->callback_closure);
	}

	if (r->handle == NULL) {
		g_free (r->buffer);
		r->buffer = NULL;
		g_string_free (r->string, TRUE);
		r->string = NULL;
		return;
	}

	r->handle = NULL;
	g_free (r->buffer);
	r->buffer = NULL;
	xml = r->string->str;
	g_string_free (r->string, FALSE);
	r->string = NULL;

	if (r->cache != NULL) {
		xmlFreeDoc (r->cache);
		r->cache = NULL;
	}

	doc = xmlParseMemory (xml, strlen (xml));
#if 0
	if (doc == NULL) {
		g_free (r->html);
		r->html = g_strdup ("<b>Error parsing XML</b>");

		e_summary_draw (r->summary);
		g_free (xml);
		return;
	}
#endif
	g_free (xml);
	r->cache = doc;

	/* Draw it */
	display_doc (r);
}

static void
read_callback (GnomeVFSAsyncHandle *handle,
	       GnomeVFSResult result,
	       gpointer buffer,
	       GnomeVFSFileSize bytes_requested,
	       GnomeVFSFileSize bytes_read,
	       RDF *r)
{
	if (result != GNOME_VFS_OK && result != GNOME_VFS_ERROR_EOF) {
		char *str;

		g_free (r->html);
		str = g_strdup_printf ("<b>%s:</b><br>%s", _("Error downloading RDF"),
				       r->uri);
		r->html = e_utf8_from_locale_string (str);

		g_free (str);

		e_summary_draw (r->summary);
		r->handle = NULL;
		gnome_vfs_async_close (handle, 
				       (GnomeVFSAsyncCloseCallback) close_callback, r);
		return;
	}

	if (bytes_read == 0) {
		gnome_vfs_async_close (handle,
				       (GnomeVFSAsyncCloseCallback) close_callback, r);
	} else {
		*((char *) buffer + bytes_read) = 0;
		g_string_append (r->string, (const char *) buffer);
		gnome_vfs_async_read (handle, buffer, 4095,
				      (GnomeVFSAsyncReadCallback) read_callback, r);
	}
}

static void
open_callback (GnomeVFSAsyncHandle *handle,
	       GnomeVFSResult result,
	       RDF *r)
{
	if (result != GNOME_VFS_OK) {
		char *str;

		g_free (r->html);
		str = g_strdup_printf ("<b>%s:</b><br>%s", _("Error downloading RDF"),
				       r->uri);
		r->html = e_utf8_from_locale_string (str);
		g_free (str);

		display_doc (r);
		return;
	}

	r->string = g_string_new ("");
	r->buffer = g_new (char, 4096);

	gnome_vfs_async_read (handle, r->buffer, 4095,
			      (GnomeVFSAsyncReadCallback) read_callback, r);
}

static gboolean
e_summary_rdf_update (ESummary *summary)
{
	GList *r;

	for (r = summary->rdf->rdfs; r; r = r->next) {
		RDF *rdf = r->data;

		gnome_vfs_async_open (&rdf->handle, rdf->uri, 
				      GNOME_VFS_OPEN_READ,
				      (GnomeVFSAsyncOpenCallback) open_callback, rdf);
	}

	return TRUE;
}
		
static void
e_summary_rdf_add_uri (ESummary *summary,
		       const char *uri)
{
	RDF *r;

	r = g_new0 (RDF, 1);
	r->summary = summary;
	r->uri = g_strdup (uri);
	r->shown = TRUE;
	summary->rdf->rdfs = g_list_append (summary->rdf->rdfs, r);
}

static void
e_summary_rdf_protocol (ESummary *summary,
			const char *uri,
			void *closure)
{
	RDF *r;
	int a;

	a = atoi (uri + 6);
	if (a == 0) {
		g_warning ("A == 0");
		return;
	}

	r = (RDF *) GINT_TO_POINTER (a);
	r->shown = !r->shown;

	display_doc (r);
}

static int
e_summary_rdf_count (ESummary *summary,
		     void *data)
{
	ESummaryRDF *rdf;
	GList *p;
	int count = 0;

	rdf = summary->rdf;
	for (p = rdf->rdfs; p; p = p->next) {
		RDF *r = p->data;

		if (r->handle != NULL) {
			count++;
		}
	}

	return count;
}

static ESummaryConnectionData *
make_connection (RDF *r)
{
	ESummaryConnectionData *d;

	d = g_new (ESummaryConnectionData, 1);
	d->hostname = g_strdup (r->uri);
	d->type = g_strdup (_("News Feed"));

	return d;
}

static GList *
e_summary_rdf_add (ESummary *summary,
		   void *data)
{
	ESummaryRDF *rdf;
	GList *p, *connections = NULL;

	rdf = summary->rdf;
	for (p = rdf->rdfs; p; p = p->next) {
		RDF *r = p->data;

		if (r->handle != NULL) {
			ESummaryConnectionData *d;

			d = make_connection (r);
			connections = g_list_prepend (connections, d);
		}
	}

	return connections;
}

static void
rdf_free (RDF *r)
{
	/* Stop the download */
	if (r->handle) {
		gnome_vfs_async_cancel (r->handle);
	}

	g_free (r->uri);
	g_free (r->html);
	g_free (r->buffer);

	if (r->string) {
		g_string_free (r->string, TRUE);
	}

	if (r->cache) {
		xmlFreeDoc (r->cache);
	}

	g_free (r);
}

static void
e_summary_rdf_set_online (ESummary *summary,
			  GNOME_Evolution_OfflineProgressListener progress,
			  gboolean online,
			  void *data)
{
	ESummaryRDF *rdf;

	rdf = summary->rdf;
	if (rdf->online == online) {
		return;
	}

	if (online == TRUE) {
		e_summary_rdf_update (summary);
		rdf->timeout = gtk_timeout_add (summary->preferences->rdf_refresh_time * 1000,
						(GtkFunction) e_summary_rdf_update,
						summary);
	} else {
		gtk_timeout_remove (rdf->timeout);
		rdf->timeout = 0;
	}

	rdf->online = online;
}

void
e_summary_rdf_init (ESummary *summary)
{
	ESummaryPrefs *prefs;
	ESummaryRDF *rdf;
	ESummaryConnection *connection;
	int timeout;

	g_return_if_fail (summary != NULL);
	g_return_if_fail (IS_E_SUMMARY (summary));

	prefs = summary->preferences;
	rdf = g_new0 (ESummaryRDF, 1);
	summary->rdf = rdf;

	connection = g_new (ESummaryConnection, 1);
	connection->count = e_summary_rdf_count;
	connection->add = e_summary_rdf_add;
	connection->set_online = e_summary_rdf_set_online;
	connection->closure = NULL;
	connection->callback = NULL;
	connection->callback_closure = NULL;

	rdf->connection = connection;
	e_summary_add_online_connection (summary, connection);

	e_summary_add_protocol_listener (summary, "rdf", e_summary_rdf_protocol, rdf);
	if (prefs == NULL) {
		e_summary_rdf_add_uri (summary, "http://www.cnn.com/cnn.rss");
		timeout = 600;
	} else {
		GList *p;

		for (p = prefs->rdf_urls; p; p = p->next) {
			e_summary_rdf_add_uri (summary, p->data);
		}
		timeout = prefs->rdf_refresh_time;
	}

	e_summary_rdf_update (summary);
	rdf->timeout = gtk_timeout_add (timeout * 1000,
					(GtkFunction) e_summary_rdf_update, summary);

	return;
}

void
e_summary_rdf_reconfigure (ESummary *summary)
{
	ESummaryRDF *rdf;
	GList *old, *p;

	g_return_if_fail (summary != NULL);
	g_return_if_fail (IS_E_SUMMARY (summary));

	rdf = summary->rdf;

	/* Stop timeout */
	gtk_timeout_remove (rdf->timeout);

	for (old = rdf->rdfs; old; old = old->next) {
		RDF *r;

		r = old->data;
		rdf_free (r);
	}
	g_list_free (rdf->rdfs);
	rdf->rdfs = NULL;

	for (p = summary->preferences->rdf_urls; p; p = p->next) {
		e_summary_rdf_add_uri (summary, p->data);
	}

	rdf->timeout = gtk_timeout_add (summary->preferences->rdf_refresh_time * 1000, (GtkFunction) e_summary_rdf_update, summary);
	e_summary_rdf_update (summary);
}

void
e_summary_rdf_free (ESummary *summary)
{
	ESummaryRDF *rdf;
	GList *p;

	g_return_if_fail (summary != NULL);
	g_return_if_fail (IS_E_SUMMARY (summary));

	rdf = summary->rdf;

	if (rdf->timeout != 0) {
		gtk_timeout_remove (rdf->timeout);
	}
	for (p = rdf->rdfs; p; p = p->next) {
		RDF *r = p->data;

		rdf_free (r);
	}
	g_list_free (rdf->rdfs);
	g_free (rdf->html);

	e_summary_remove_online_connection (summary, rdf->connection);
	g_free (rdf->connection);

	g_free (rdf);
	summary->rdf = NULL;
}
