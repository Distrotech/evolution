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

/*
  Abstract class for formatting mime messages
*/

#ifndef _EM_FORMAT_H
#define _EM_FORMAT_H

#include <glib-object.h>
#include "e-util/e-msgport.h"

struct _CamelStream;
struct _CamelMimePart;
struct _CamelMedium;
struct _CamelSession;
struct _CamelURL;

typedef struct _EMFormat EMFormat;
typedef struct _EMFormatClass EMFormatClass;

typedef struct _EMFormatHandler EMFormatHandler;
typedef struct _EMFormatHeader EMFormatHeader;

typedef void (*EMFormatFunc) (EMFormat *md, struct _CamelStream *stream, struct _CamelMimePart *part, const EMFormatHandler *info);

/* can be subclassed/extended ... */
struct _EMFormatHandler {
	char *mime_type;
	EMFormatFunc handler;
	GList *applications;	/* gnome vfs short-list of applications, do we care? */
};

typedef struct _EMFormatPURI EMFormatPURI;
typedef void (*EMFormatPURIFunc)(EMFormat *md, struct _CamelStream *stream, EMFormatPURI *puri);

struct _EMFormatPURI {
	struct _EMFormatPURI *next, *prev;

	struct _EMFormat *format;

	char *uri;		/* will be the location of the part, may be empty */
	char *cid;		/* will always be set, a fake one created if needed */

	EMFormatPURIFunc func;
	struct _CamelMimePart *part;

	unsigned int use_count;	/* used by multipart/related to see if it was accessed */
};

/* used to stack pending uri's for visibility (multipart/related) */
struct _EMFormatPURITree {
	struct _EMFormatPURITree *next, *prev, *parent;

	EDList uri_list;
	EDList children;
};

struct _EMFormatHeader {
	struct _EMFormatHeader *next, *prev;

	guint32 flags;		/* E_FORMAT_HEADER_* */
	char name[1];
};

#define EM_FORMAT_HEADER_BOLD (1<<0)
#define EM_FORMAT_HEADER_LAST (1<<4) /* reserve 4 slots */

struct _EMFormat {
	GObject parent;

	struct _CamelMedium *message; /* the current message */

	EDList header_list;	/* if empty, then all */

	struct _CamelSession *session; /* session, used for authentication when required */
	struct _CamelURL *base;		/* current location (base url) */

	/* for forcing inlining */
	GHashTable *inline_table;

	/* global lookup table for message */
	GHashTable *pending_uri_table;

	/* visibility tree, also stores every puri permanently */
	struct _EMFormatPURITree *pending_uri_tree;
	/* current level to search from */
	struct _EMFormatPURITree *pending_uri_level;
};

struct _EMFormatClass {
	GObjectClass parent_class;

	GHashTable *type_handlers;

	/* start formatting a message */
	void (*format)(EMFormat *, struct _CamelMedium *);
	/* some internel error/inconsistency */
	void (*format_error)(EMFormat *, struct _CamelStream *, const char *msg);

	/* use for external structured parts */
	void (*format_attachment)(EMFormat *, struct _CamelStream *, struct _CamelMimePart *, const char *mime_type, const struct _EMFormatHandler *info);
	/* for any message parts */
	void (*format_message)(EMFormat *, struct _CamelStream *, struct _CamelMedium *);
	/* use for unparsable content */
	void (*format_source)(EMFormat *, struct _CamelStream *, struct _CamelMimePart *);
};

/* clones inline state/view, or use to redraw */
void em_format_format_clone(EMFormat *emf, struct _CamelMedium *msg, EMFormat *emfsource);
/* helper entry point */
#define em_format_format(emf, msg) em_format_format_clone((emf), (msg), NULL)
void em_format_set_session(EMFormat *emf, struct _CamelSession *s);

void em_format_clear_headers(EMFormat *emf); /* also indicates to show all headers */
void em_format_default_headers(EMFormat *emf);
void em_format_add_header(EMFormat *emf, const char *name, guint32 flags);

/* FIXME: Need a 'clone' api to copy details about the current view (inlines etc)
   Or maybe it should live with sub-classes? */

int em_format_is_attachment(EMFormat *emf, struct _CamelMimePart *part);
int em_format_is_inline(EMFormat *emf, struct _CamelMimePart *part);
/* FIXME: not sure about this api */
void em_format_set_inline(EMFormat *emf, struct _CamelMimePart *part, int state);
char *em_format_describe_part(struct _CamelMimePart *part, const char *mimetype);

/* for implementers */
GType em_format_get_type(void);

void em_format_class_add_handler(EMFormatClass *emfc, EMFormatHandler *info);
const EMFormatHandler *em_format_find_handler(EMFormat *emf, const char *mime_type);
const EMFormatHandler *em_format_fallback_handler(EMFormat *emf, const char *mime_type);

/* puri is short for pending uri ... really */
EMFormatPURI *em_format_add_puri(EMFormat *emf, size_t size, const char *uri, struct _CamelMimePart *part, EMFormatPURIFunc func);
EMFormatPURI *em_format_find_visible_puri(EMFormat *emf, const char *uri);
EMFormatPURI *em_format_find_puri(EMFormat *emf, const char *uri);
void em_format_clear_puri_tree(EMFormat *emf);
void em_format_push_level(EMFormat *emf);
void em_format_pull_level(EMFormat *emf);

#define em_format_format_error(emf, stream, txt) ((EMFormatClass *)G_OBJECT_GET_CLASS(emf))->format_error((emf), (stream), (txt))
#define em_format_format_attachment(emf, stream, msg, type, info) ((EMFormatClass *)G_OBJECT_GET_CLASS(emf))->format_attachment((emf), (stream), (msg), (type), (info))
#define em_format_format_message(emf, stream, msg) ((EMFormatClass *)G_OBJECT_GET_CLASS(emf))->format_message((emf), (stream), (msg))
#define em_format_format_source(emf, stream, msg) ((EMFormatClass *)G_OBJECT_GET_CLASS(emf))->format_source((emf), (stream), (msg))

/* raw content, but also charset override where applicable */
/* should this also be virtual? */
void em_format_format_content(EMFormat *emf, struct _CamelStream *stream, struct _CamelMimePart *part);

void em_format_part_as(EMFormat *emf, struct _CamelStream *stream, struct _CamelMimePart *part, const char *mime_type);
void em_format_part(EMFormat *emf, struct _CamelStream *stream, struct _CamelMimePart *part);

#endif /* ! _EM_FORMAT_H */
