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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include <gal/util/e-iconv.h>

#include <gtkhtml/gtkhtml.h>
#include <gtkhtml/gtkhtml-embedded.h>
#include <gtkhtml/gtkhtml-stream.h>
#include <gtkhtml/htmlengine.h>

#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>

#include <camel/camel-mime-message.h>
#include <camel/camel-stream.h>
#include <camel/camel-stream-filter.h>
#include <camel/camel-mime-filter.h>
#include <camel/camel-mime-filter-tohtml.h>
#include <camel/camel-mime-filter-enriched.h>
#include <camel/camel-multipart.h>
#include <camel/camel-multipart-signed.h>
#include <camel/camel-gpg-context.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-url.h>
#include <camel/camel-stream-fs.h>
#include <camel/camel-string-utils.h>

#include <e-util/e-msgport.h>

#include "em-format-html.h"
#include "em-camel-stream.h"

#define EFH_TABLE_OPEN "<table>"

struct _EMFormatHTMLPrivate {
	struct _CamelMedium *last_part;	/* not reffed, DO NOT dereference */
};

static void efh_url_requested(GtkHTML *html, const char *url, GtkHTMLStream *handle, EMFormatHTML *efh);
static gboolean efh_object_requested(GtkHTML *html, GtkHTMLEmbedded *eb, EMFormatHTML *efh);

static void efh_format(EMFormat *, CamelMedium *);
static void efh_format_error(EMFormat *emf, CamelStream *stream, const char *txt);
static void efh_format_message(EMFormat *, CamelStream *, CamelMedium *);
static void efh_format_source(EMFormat *, CamelStream *, CamelMimePart *);
static void efh_format_attachment(EMFormat *, CamelStream *, CamelMimePart *, const char *, const EMFormatHandler *);

static void efh_builtin_init(EMFormatHTMLClass *efhc);

static void efh_write_data(EMFormat *emf, CamelStream *stream, EMFormatPURI *puri);

static EMFormatClass *efh_parent;

static void
efh_init(GObject *o)
{
	EMFormatHTML *efh = (EMFormatHTML *)o;

	efh->priv = g_malloc0(sizeof(*efh->priv));

	e_dlist_init(&efh->pending_object_list);

	efh->html = (GtkHTML *)gtk_html_new();
	g_object_ref(efh->html);
	gtk_object_sink((GtkObject *)efh->html);

	gtk_html_set_default_content_type(efh->html, "text/html; charset=utf-8");
	gtk_html_set_editable(efh->html, FALSE);
	
	g_signal_connect(efh->html, "url_requested", G_CALLBACK(efh_url_requested), efh);
	g_signal_connect(efh->html, "object_requested", G_CALLBACK(efh_object_requested), efh);

	efh->header_colour = 0xeeeeee;
	efh->text_colour = 0;
	efh->text_html_flags = CAMEL_MIME_FILTER_TOHTML_CONVERT_NL | CAMEL_MIME_FILTER_TOHTML_CONVERT_SPACES;
	/* CAMEL_MIME_FILTER_TOHTML_MARK_CITATION ? */
}

static void
efh_finalise(GObject *o)
{
	EMFormatHTML *efh = (EMFormatHTML *)o;

	em_format_html_clear_pobject(efh);

	if (efh->html)
		g_object_unref(efh->html);

	g_free(efh->priv);

	((GObjectClass *)efh_parent)->finalize(o);
}

static void
efh_class_init(GObjectClass *klass)
{
	efh_builtin_init((EMFormatHTMLClass *)klass);

	((EMFormatClass *)klass)->format = efh_format;
	((EMFormatClass *)klass)->format_error = efh_format_error;
	((EMFormatClass *)klass)->format_message = efh_format_message;
	((EMFormatClass *)klass)->format_source = efh_format_source;
	((EMFormatClass *)klass)->format_attachment = efh_format_attachment;

	klass->finalize = efh_finalise;
}

GType
em_format_html_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMFormatHTMLClass),
			NULL, NULL,
			(GClassInitFunc)efh_class_init,
			NULL, NULL,
			sizeof(EMFormatHTML), 0,
			(GInstanceInitFunc)efh_init
		};
		efh_parent = g_type_class_ref(em_format_get_type());
		type = g_type_register_static(em_format_get_type(), "EMFormatHTML", &info, 0);
	}

	return type;
}

EMFormatHTML *em_format_html_new(void)
{
	EMFormatHTML *efh;

	efh = g_object_new(em_format_html_get_type(), 0);

	return efh;
}

CamelMimePart *
em_format_html_file_part(EMFormatHTML *efh, const char *mime_type, const char *path, const char *name)
{
	CamelMimePart *part;
	CamelStream *stream;
	CamelDataWrapper *dw;
	char *filename;

	filename = g_build_filename(path, name, NULL);
	stream = camel_stream_fs_new_with_name(filename, O_RDONLY, 0);
	g_free(filename);
	if (stream == NULL)
		return NULL;

	part = camel_mime_part_new();
	dw = camel_data_wrapper_new();
	camel_data_wrapper_construct_from_stream(dw, stream);
	camel_object_unref(stream);
	if (mime_type)
		camel_data_wrapper_set_mime_type(dw, mime_type);
	part = camel_mime_part_new();
	camel_medium_set_content_object((CamelMedium *)part, dw);
	camel_object_unref(dw);
	camel_mime_part_set_filename(part, name);

	return part;
}

/* all this api is a pain in the bum ... */

/* should it have a user-data field? */
const char *
em_format_html_add_pobject(EMFormatHTML *efh, const char *classid, EMFormatHTMLPObjectFunc func, CamelMimePart *part)
{
	EMFormatHTMLPObject *pobj;

	pobj = g_malloc(sizeof(*pobj));
	if (classid) {
		pobj->classid = g_strdup(classid);
	} else {
		static unsigned int uriid = 0;

		pobj->classid = g_strdup_printf("e-object:///%u", uriid++);
	}

	pobj->format = efh;
	pobj->func = func;
	pobj->part = part;

	e_dlist_addtail(&efh->pending_object_list, (EDListNode *)pobj);

	return pobj->classid;
}

EMFormatHTMLPObject *
em_format_html_find_pobject(EMFormatHTML *emf, const char *classid)
{
	EMFormatHTMLPObject *pw;

	pw = (EMFormatHTMLPObject *)emf->pending_object_list.head;
	while (pw->next) {
		if (!strcmp(pw->classid, classid))
			return pw;
		pw = pw->next;
	}

	return NULL;
}

EMFormatHTMLPObject *
em_format_html_find_pobject_func(EMFormatHTML *emf, CamelMimePart *part, EMFormatHTMLPObjectFunc func)
{
	EMFormatHTMLPObject *pw;

	pw = (EMFormatHTMLPObject *)emf->pending_object_list.head;
	while (pw->next) {
		if (pw->func == func && pw->part == part)
			return pw;
		pw = pw->next;
	}

	return NULL;
}

void
em_format_html_remove_pobject(EMFormatHTML *emf, EMFormatHTMLPObject *pobject)
{
	e_dlist_remove((EDListNode *)pobject);
	g_free(pobject->classid);
	g_free(pobject);
}

void
em_format_html_clear_pobject(EMFormatHTML *emf)
{
	while (!e_dlist_empty(&emf->pending_object_list))
		em_format_html_remove_pobject(emf, (EMFormatHTMLPObject *)emf->pending_object_list.head);
}

/* ********************************************************************** */

static void
efh_url_requested(GtkHTML *html, const char *url, GtkHTMLStream *handle, EMFormatHTML *efh)
{
	EMFormatPURI *puri;

	printf("url requested, html = %p\n", html);

	puri = em_format_find_visible_puri((EMFormat *)efh, url);
	if (puri) {
		CamelStream *estream = em_camel_stream_new(handle);

		/* how to stop recursion? e_dlist_remove((EDListNode *)puri);*/
		puri->use_count++;
		puri->func((EMFormat *)efh, estream, puri);
		camel_object_unref(estream);
	} else if (g_ascii_strncasecmp(url, "http:", 5) == 0 || g_ascii_strncasecmp(url, "https:", 6) == 0) {
		/* FIXME: download stuff */
		gtk_html_stream_close(handle, GTK_HTML_STREAM_ERROR);
	} else {
		printf("HTML Includes reference to unknown uri '%s'\n", url);
		gtk_html_stream_close(handle, GTK_HTML_STREAM_ERROR);
	}
}

static gboolean
efh_object_requested(GtkHTML *html, GtkHTMLEmbedded *eb, EMFormatHTML *efh)
{
	EMFormatHTMLPObject *pobject;
	int res = FALSE;

	if (eb->classid == NULL)
		return FALSE;

	pobject = em_format_html_find_pobject(efh, eb->classid);
	if (pobject) {
		/* This stops recursion of the part */
		e_dlist_remove((EDListNode *)pobject);
		res = pobject->func(efh, eb, pobject);
		e_dlist_addhead(&efh->pending_object_list, (EDListNode *)pobject);
	} else {
		printf("HTML Includes reference to unknown object '%s'\n", eb->classid);
	}

	return res;
}

/* ********************************************************************** */

static void
efh_text_plain(EMFormatHTML *efh, CamelStream *stream, CamelMimePart *part, EMFormatHandler *info)
{
	CamelStreamFilter *filtered_stream;
	CamelMimeFilter *html_filter;
	CamelContentType *type;
	const char *format;
	guint32 rgb = 0x737373, flags;

	/* FIXME: charset override? */

	camel_stream_write_string(stream, EFH_TABLE_OPEN "<tr><td><tt>\n");

	flags = efh->text_html_flags;

	/* FIXME: citation stuff ?*/

	/* Check for RFC 2646 flowed text. */
	type = camel_mime_part_get_content_type(part);
	if (header_content_type_is(type, "text", "plain")
	    && (format = header_content_type_param(type, "format"))
	    && !g_ascii_strcasecmp(format, "flowed"))
		flags |= CAMEL_MIME_FILTER_TOHTML_FORMAT_FLOWED;
	
	html_filter = camel_mime_filter_tohtml_new(flags, rgb);
	filtered_stream = camel_stream_filter_new_with_stream(stream);
	camel_stream_filter_add(filtered_stream, html_filter);
	camel_object_unref(html_filter);
	camel_data_wrapper_decode_to_stream(camel_medium_get_content_object((CamelMedium *)part), (CamelStream *)filtered_stream);
	camel_object_unref(filtered_stream);
	
	camel_stream_write_string(stream, "</tt></td></tr></table>\n");
}

static void
efh_write_wrapper(EMFormat *emf, CamelStream *stream, EMFormatPURI *puri)
{
	em_format_format_content(emf, stream, puri->part);
}

static void
efh_text_enriched(EMFormatHTML *efh, CamelStream *stream, CamelMimePart *part, EMFormatHandler *info)
{
	CamelStreamFilter *filtered_stream;
	CamelMimeFilter *enriched;
	CamelDataWrapper *dw;
	guint32 flags = 0;
	
	dw = camel_medium_get_content_object((CamelMedium *)part);
	
	if (!strcmp(info->mime_type, "text/richtext")) {
		flags = CAMEL_MIME_FILTER_ENRICHED_IS_RICHTEXT;
		camel_stream_write_string( stream, "\n<!-- text/richtext -->\n");
	} else {
		camel_stream_write_string( stream, "\n<!-- text/enriched -->\n");
	}
	
	enriched = camel_mime_filter_enriched_new(flags);
	filtered_stream = camel_stream_filter_new_with_stream( stream);
	camel_stream_filter_add(filtered_stream, enriched);
	camel_object_unref(enriched);
	
	camel_stream_write_string(stream, EFH_TABLE_OPEN "<tr><td><tt>\n");	
	camel_data_wrapper_decode_to_stream(dw, (CamelStream *)filtered_stream);
	
	camel_stream_write_string(stream, "</tt></td></tr></table>\n");
	camel_object_unref(filtered_stream);
}

static void
efh_text_html(EMFormatHTML *efh, CamelStream *stream, CamelMimePart *part, EMFormatHandler *info)
{
	const char *location, *base;
	EMFormatPURI *puri;

	camel_stream_write_string(stream, "\n<!-- text/html -->\n");
	
	if ((base = camel_medium_get_header((CamelMedium *)part, "Content-Base"))) {
		char *base_url;
		size_t len;
		
		len = strlen(base);
		if (*base == '"' && *(base + len - 1) == '"') {
			len -= 2;
			base_url = alloca(len + 1);
			memcpy(base_url, base + 1, len);
			base_url[len] = '\0';
			base = base_url;
		}
		
		gtk_html_set_base(efh->html, base);
	}

	puri = em_format_add_puri((EMFormat *)efh, sizeof(EMFormatPURI), NULL, part, efh_write_wrapper);
	location = puri->uri?puri->uri:puri->cid;
	camel_stream_printf ( stream,
			     "<iframe src=\"%s\" frameborder=0 scrolling=no>could not get %s</iframe>",
			      location, location);
}

/* This is a lot of code for something useless ... */
static void
efh_message_external(EMFormatHTML *efh, CamelStream *stream, CamelMimePart *part, EMFormatHandler *info)
{
	CamelContentType *type;
	const char *access_type;
	char *url = NULL, *desc = NULL;

	/* needs to be cleaner */
	type = camel_mime_part_get_content_type(part);
	access_type = header_content_type_param(type, "access-type");
	if (!access_type) {
		camel_stream_printf(stream, _("Malformed external-body part."));
		return;
	}
	
	if (!g_ascii_strcasecmp(access_type, "ftp") ||
	    !g_ascii_strcasecmp(access_type, "anon-ftp")) {
		const char *name, *site, *dir, *mode;
		char *path;
		char ftype[16];

		name = header_content_type_param(type, "name");
		site = header_content_type_param(type, "site");
		dir = header_content_type_param(type, "directory");
		mode = header_content_type_param(type, "mode");
		if (name == NULL || site == NULL)
			goto fail;
		
		/* Generate the path. */
		if (dir)
			path = g_strdup_printf("/%s/%s", *dir=='/'?dir+1:dir, name);
		else
			path = g_strdup_printf("/%s", *name=='/'?name+1:name);

		if (mode && &mode)
			sprintf(ftype, ";type=%c",  *mode);
		else
			ftype[0] = 0;
		
		url = g_strdup_printf ("ftp://%s%s%s", site, path, ftype);
		g_free (path);
		desc = g_strdup_printf (_("Pointer to FTP site (%s)"), url);
	} else if (!g_ascii_strcasecmp (access_type, "local-file")) {
		const char *name, *site;
		
		name = header_content_type_param (type, "name");
		site = header_content_type_param (type, "site");
		if (name == NULL)
			goto fail;
		
		url = g_strdup_printf ("file:///%s", *name == '/' ? name+1:name);
		if (site)
			desc = g_strdup_printf(_("Pointer to local file (%s) valid at site \"%s\""), name, site);
		else
			desc = g_strdup_printf(_("Pointer to local file (%s)"), name);
	} else if (!g_ascii_strcasecmp (access_type, "URL")) {
		const char *urlparam;
		char *s, *d;
		
		/* RFC 2017 */
		
		urlparam = header_content_type_param (type, "url");
		if (urlparam == NULL)
			goto fail;
		
		/* For obscure MIMEy reasons, the URL may be split into words */
		url = g_strdup (urlparam);
		s = d = url;
		while (*s) {
			/* FIXME: use camel_isspace */
			if (!isspace ((unsigned char)*s))
				*d++ = *s;
			s++;
		}
		*d = 0;
		desc = g_strdup_printf (_("Pointer to remote data (%s)"), url);
	} else
		goto fail;

	camel_stream_printf(stream, "<a href=\"%s\">%s</a>", url, desc);
	g_free(url);
	g_free(desc);

	return;

fail:
	camel_stream_printf(stream, _("Pointer to unknown external data (\"%s\" type)"), access_type);
}

static const struct {
	const char *icon;
	const char *text;
} signed_table[2] = {
	{ "pgp-signature-bad.png", N_("This message is digitally signed but can not be proven to be authentic.") },
	{ "pgp-signature-ok.png", N_("This message is digitally signed and has been found to be authentic.") }
};

void
em_format_html_multipart_signed_sign(EMFormat *emf, CamelStream *stream, CamelMimePart *part)
{
	CamelMimePart *spart;
	CamelMultipartSigned *mps;
	CamelCipherValidity *valid = NULL;
	CamelException ex;
	const char *message = NULL;
	int good = 0;
	CamelCipherContext *cipher;
	char *classid;
	EMFormatPURI *iconpuri;
	CamelMimePart *iconpart;
	static int iconid;

	mps = (CamelMultipartSigned *)camel_medium_get_content_object((CamelMedium *)part);
	spart = camel_multipart_get_part((CamelMultipart *)mps, CAMEL_MULTIPART_SIGNED_SIGNATURE);
	camel_exception_init(&ex);
	if (spart == NULL) {
		message = _("No signature present");
	} else if (emf->session == NULL) {
		message = _("Session not initialised");
	} else if ((cipher = camel_gpg_context_new(emf->session)) == NULL) {
		message = _("Could not create signature verfication context");
	} else {
		valid = camel_multipart_signed_verify(mps, cipher, &ex);
		camel_object_unref(cipher);
		if (valid) {
			good = camel_cipher_validity_get_valid(valid)?1:0;
			message = camel_cipher_validity_get_description(valid);
		} else {
			message = camel_exception_get_description(&ex);
		}
	}

	classid = g_strdup_printf("multipart-signed:///em-format-html/%p/icon/%d", part, iconid++);
	iconpart = em_format_html_file_part((EMFormatHTML *)emf, "image/png", EVOLUTION_ICONSDIR, signed_table[good].icon);
	if (iconpart) {
		iconpuri = em_format_add_puri(emf, sizeof(*iconpuri), classid, iconpart, efh_write_data);
		camel_object_unref(iconpart);
	}

	camel_stream_printf(stream, "<table><tr valign=top>"
			    "<td><img src=\"%s\"></td>"
			    "<td>%s<br><br>",
			    classid,
			    _(signed_table[good].text));
	g_free(classid);

	if (message) {
		char *tmp = camel_text_to_html(message, ((EMFormatHTML *)emf)->text_html_flags, 0);

		camel_stream_printf(stream, "<font size=-1%s>%s</font>", good?"":" color=red", tmp);
		g_free(tmp);
	}
		
	camel_stream_write_string(stream, "</td></tr></table>");
	
	camel_exception_clear(&ex);
	camel_cipher_validity_free(valid);
}

static void
efh_multipart_signed(EMFormat *emf, CamelStream *stream, CamelMimePart *part, const EMFormatHandler *info)
{
	CamelMimePart *cpart;
	CamelMultipartSigned *mps;

	mps = (CamelMultipartSigned *)camel_medium_get_content_object((CamelMedium *)part);
	if (!CAMEL_IS_MULTIPART_SIGNED(mps)
	    || (cpart = camel_multipart_get_part((CamelMultipart *)mps, CAMEL_MULTIPART_SIGNED_CONTENT)) == NULL) {
		em_format_format_source(emf, stream, part);
		return;
	}

	em_format_part(emf, stream, cpart);
	em_format_html_multipart_signed_sign(emf, stream, part);
}

static void
efh_write_data(EMFormat *emf, CamelStream *stream, EMFormatPURI *puri)
{
	CamelDataWrapper *dw = camel_medium_get_content_object((CamelMedium *)puri->part);

	printf("writing image '%s'\n", puri->uri?puri->uri:puri->cid);
	camel_data_wrapper_decode_to_stream(dw, stream);
	camel_stream_close(stream);
}

static void
efh_image(EMFormatHTML *efh, CamelStream *stream, CamelMimePart *part, EMFormatHandler *info)
{
	EMFormatPURI *puri;
	const char *location;

	puri = em_format_add_puri((EMFormat *)efh, sizeof(EMFormatPURI), NULL, part, efh_write_data);
	location = puri->uri?puri->uri:puri->cid;
	printf("adding image '%s'\n", location);
	camel_stream_printf(stream, "<img hspace=10 vspace=10 src=\"%s\">", location);
}

static EMFormatHandler type_builtin_table[] = {
	{ "image/gif", (EMFormatFunc)efh_image },
	{ "image/jpeg", (EMFormatFunc)efh_image },
	{ "image/png", (EMFormatFunc)efh_image },
	{ "image/x-png", (EMFormatFunc)efh_image },
	{ "image/tiff", (EMFormatFunc)efh_image },
	{ "image/x-bmp", (EMFormatFunc)efh_image },
	{ "image/bmp", (EMFormatFunc)efh_image },
	{ "image/x-cmu-raster", (EMFormatFunc)efh_image },
	{ "image/x-ico", (EMFormatFunc)efh_image },
	{ "image/x-portable-anymap", (EMFormatFunc)efh_image },
	{ "image/x-portable-bitmap", (EMFormatFunc)efh_image },
	{ "image/x-portable-graymap", (EMFormatFunc)efh_image },
	{ "image/x-portable-pixmap", (EMFormatFunc)efh_image },
	{ "image/x-xpixmap", (EMFormatFunc)efh_image },
	{ "text/enriched", (EMFormatFunc)efh_text_enriched },
	{ "text/plain", (EMFormatFunc)efh_text_plain },
	{ "text/html", (EMFormatFunc)efh_text_html },
	{ "text/richtext", (EMFormatFunc)efh_text_enriched },
	{ "text/*", (EMFormatFunc)efh_text_enriched },
	{ "message/external-body", (EMFormatFunc)efh_message_external },
	{ "multipart/signed", (EMFormatFunc)efh_multipart_signed },
};

static void
efh_builtin_init(EMFormatHTMLClass *efhc)
{
	int i;

	for (i=0;i<sizeof(type_builtin_table)/sizeof(type_builtin_table[0]);i++)
		em_format_class_add_handler((EMFormatClass *)efhc, &type_builtin_table[i]);
}

static void efh_format(EMFormat *emf, CamelMedium *part)
{
	EMFormatHTML *efh = (EMFormatHTML *)emf;
	GtkHTMLStream *hstream;
	CamelStream *estream;

	em_format_html_clear_pobject(efh);

		if (efh->priv->last_part == part) {
			printf("doing same page, turning off new page ...\n");
			/* HACK: so we redraw in the same spot */
			efh->html->engine->newPage = FALSE;
		}

	hstream = gtk_html_begin(efh->html);
	estream = em_camel_stream_new(hstream);
	if (part) {
		if (efh->priv->last_part == part) {
			printf("doing same page, turning off new page ...\n");
			/* HACK: so we redraw in the same spot */
			efh->html->engine->newPage = FALSE;
		} else
			efh->priv->last_part = part;

		/* <insert top-header stuff here> */
		/* How to sub-class ?  Might need to adjust api ... */

		em_format_format_message(emf, estream, part);
	}
	camel_stream_close(estream);
	camel_object_unref(estream);
}

static void efh_format_error(EMFormat *emf, CamelStream *stream, const char *txt)
{
	char *html;

	html = camel_text_to_html (txt, CAMEL_MIME_FILTER_TOHTML_CONVERT_NL|CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS, 0);
	camel_stream_printf(stream, "<em><font color=\"red\">%s</font></em><br>", html);
	g_free(html);
}

static void
efh_format_text_header(EMFormat *emf, CamelStream *stream, const char *label, const char *value, guint32 flags)
{
	char *html;
	const char *fmt;

	if (value == NULL)
		return;

	while (*value == ' ')
		value++;

	html = camel_text_to_html(value, ((EMFormatHTML *)emf)->text_html_flags, 0);

	if (flags & EM_FORMAT_HTML_HEADER_NOCOLUMNS) {
		if (flags & EM_FORMAT_HEADER_BOLD)
			fmt = "<tr><td><b>%s:</b> %s</td></tr>";
		else
			fmt = "<tr><td>%s: %s</td></tr>";
	} else {
		if (flags & EM_FORMAT_HEADER_BOLD)
			fmt = "<tr><th align=\"right\" valign=\"top\">%s:<b>&nbsp;</b></th><td>%s</td></tr>";
		else
			fmt = "<tr><td align=\"right\" valign=\"top\">%s:<b>&nbsp;</b></td><td>%s</td></tr>";
	}

	camel_stream_printf(stream, fmt, label, html);
	g_free(html);
}

static void
efh_format_address(EMFormat *emf, CamelStream *stream, const CamelInternetAddress *cia, const char *name, guint32 flags)
{
	char *text;

	if (cia == NULL || !camel_internet_address_get(cia, 0, NULL, NULL))
		return;

	text = camel_address_format((CamelAddress *)cia);
	efh_format_text_header(emf, stream, name, text, flags | EM_FORMAT_HEADER_BOLD);
	g_free(text);
}

static void
efh_format_header(EMFormat *emf, CamelStream *stream, CamelMedium *part, const char *namein, guint32 flags, const char *charset)
{
#define msg ((CamelMimeMessage *)part)
	char *name;

	name = alloca(strlen(namein)+1);
	strcpy(name, namein);
	camel_strdown(name);

	if (!strcmp(name, "from"))
		efh_format_address(emf, stream, camel_mime_message_get_from(msg), _("From"), flags);
	else if (!strcmp(name, "reply-to"))
		efh_format_address(emf, stream, camel_mime_message_get_reply_to(msg), _("Reply-To"), flags);
	else if (!strcmp(name, "to"))
		efh_format_address(emf, stream, camel_mime_message_get_recipients(msg, CAMEL_RECIPIENT_TYPE_TO), _("To"), flags);
	else if (!strcmp(name, "cc"))
		efh_format_address(emf, stream, camel_mime_message_get_recipients(msg, CAMEL_RECIPIENT_TYPE_CC), _("Cc"), flags);
	else if (!strcmp(name, "bcc"))
		efh_format_address(emf, stream, camel_mime_message_get_recipients(msg, CAMEL_RECIPIENT_TYPE_BCC), _("Bcc"), flags);
	else {
		const char *txt, *label;
		char *value = NULL;

		if (!strcmp(name, "subject")) {
			txt = camel_mime_message_get_subject(msg);
			label = _("Subject");
			flags |= EM_FORMAT_HEADER_BOLD;
		} else if (!strcmp(name, "x-evolution-mailer")) { /* pseudo-header */
			txt = camel_medium_get_header(part, "x-mailer");
			if (txt == NULL)
				txt = camel_medium_get_header(part, "user-agent");
			label = _("Mailer");
			flags |= EM_FORMAT_HEADER_BOLD;
		} else if (!strcmp(name, "date")) {
			txt = camel_medium_get_header(part, "date");
			label = _("Date");
			flags |= EM_FORMAT_HEADER_BOLD;
		} else {
			txt = camel_medium_get_header(part, name);
			value = header_decode_string(txt, charset);
			txt = value;
			label = namein;
		}

		efh_format_text_header(emf, stream, label, txt, flags);

		if (value)
			g_free(value);
	}
#undef msg
}

static void efh_format_message(EMFormat *emf, CamelStream *stream, CamelMedium *part)
{
	EMFormatHeader *h;
	const char *charset;
	CamelContentType *ct;
#define efh ((EMFormatHTML *)emf)
		
	ct = camel_mime_part_get_content_type((CamelMimePart *)part);
	charset = header_content_type_param(ct, "charset");
	charset = e_iconv_charset_name(charset);	

	camel_stream_printf(stream,
			    "<table width=\"100%%\" cellpadding=5 cellspacing=0>"
			    "<tr><td>"
			    "<table width=\"100%%\" cellpaddding=1 cellspacing=0 bgcolor=\"#000000\">"
			    "<tr><td>"
			    "<table width=\"100%%\"cellpadding=0 cellspacing=0 bgcolor=\"#%06x\">"
			    "<tr><td>"
			    "<table><font color=\"#%06x\"",
			    efh->header_colour & 0xffffff,
			    efh->text_colour & 0xffffff);

	/* dump selected headers */
	h = (EMFormatHeader *)emf->header_list.head;
	if (h->next == NULL) {
		struct _header_raw *header;
		
		header = ((CamelMimePart *)part)->headers;
		while (header) {
			efh_format_header(emf, stream, part, header->name, EM_FORMAT_HTML_HEADER_NOCOLUMNS, charset);
			header = header->next;
		}
	} else {
		while (h->next) {
			efh_format_header(emf, stream, part, h->name, h->flags, charset);
			h = h->next;
		}
	}

	camel_stream_printf(stream,
			    "</font></table>"
			    "</td></tr></table>"
			    "</td></tr></table>"
			    "</td></tr></table>");

	if (emf->message != part)
		camel_stream_printf(stream, "<blockquote>");

	em_format_part(emf, stream, (CamelMimePart *)part);

	if (emf->message != part)
		camel_stream_printf(stream, "</blockquote>");
#undef efh
}

static void efh_format_source(EMFormat *emf, CamelStream *stream, CamelMimePart *part)
{
	camel_stream_printf(stream, "<em><font color=\"red\">%s</font></em>", _("Could not parse MIME message. Displaying as source."));
	efh_text_plain((EMFormatHTML *)emf, stream, part, NULL);
}

static void
efh_format_attachment(EMFormat *emf, CamelStream *stream, CamelMimePart *part, const char *mime_type, const EMFormatHandler *handle)
{
	const char *text;
	char *html;
	GString *stext;

	/* we display all inlined attachments only */

	/* this could probably be cleaned up ... */
	camel_stream_write_string(stream,
				  "<table cellspacing=0 cellpadding=0><tr><td>"
				  "<table width=10 cellspacing=0 cellpadding=0>"
				  "<tr><td></td></tr></table></td>"
				  "<td><table width=3 cellspacing=0 cellpadding=0>"
				  "<tr><td></td></tr></table></td><td><font size=-1>");

	/* output some info about it */
	stext = g_string_new("");
	text = gnome_vfs_mime_get_description(mime_type);
	g_string_append_printf(stext, _("%s attachment"), text?text:mime_type);
	if ((text = camel_mime_part_get_filename (part)))
		g_string_append_printf(stext, " (%s)", text);
	if ((text = camel_mime_part_get_description(part)))
		g_string_append_printf(stext, ", \"%s\"", text);

	html = camel_text_to_html(stext->str, ((EMFormatHTML *)emf)->text_html_flags & CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS, 0);
	camel_stream_write_string(stream, html);
	g_free(html);
	g_string_free(stext, TRUE);

	camel_stream_write_string(stream, "</font></td></tr><tr></table>");

	if (handle && em_format_is_inline(emf, part))
		handle->handler(emf, stream, part, handle);
}
