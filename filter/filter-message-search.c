/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@helixcode.com>
 *
 *  Copyright 2000 Helix Code, Inc. (www.helixcode.com)
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

#include "filter-message-search.h"
#include <e-util/e-sexp.h>
#include <regex.h>

typedef struct {
	CamelMimeMessage *message;
	CamelMessageInfo *info;
	CamelException *ex;
} FilterMessageSearch;

/* ESExp callbacks */
static ESExpResult *header_contains (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *match_all (struct _ESExp *f, int argc, struct _ESExpTerm **argv, FilterMessageSearch *fms);
static ESExpResult *body_contains (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *user_flag (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *user_tag (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *get_sent_date (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *get_received_date (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *get_current_date (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);
static ESExpResult *get_score (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms);

/* builtin functions */
static struct {
	char *name;
	ESExpFunc *func;
	int type;		/* set to 1 if a function can perform shortcut evaluation, or
				   doesn't execute everything, 0 otherwise */
} symbols[] = {
	{ "match-all",         (ESExpFunc *) match_all,         0 },
	{ "body-contains",     (ESExpFunc *) body_contains,     0 },
	{ "header-contains",   (ESExpFunc *) header_contains,   0 },
	{ "user-tag",          (ESExpFunc *) user_tag,          0 },
	{ "user-flag",         (ESExpFunc *) user_flag,         0 },
	{ "get-sent-date",     (ESExpFunc *) get_sent_date,     0 },
	{ "get-received-date", (ESExpFunc *) get_received_date, 0 },
	{ "get-current-date",  (ESExpFunc *) get_current_date,  0 },
	{ "get-score",         (ESExpFunc *) get_score,         0 }
};

static ESExpResult *
header_contains (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	gboolean matched = FALSE;
	ESExpResult *r;
	
	if (argc == 2) {
		char *header = (argv[0])->value.string;
		char *match = (argv[1])->value.string;
		regex_t regexpat;        /* regex patern */
		regmatch_t *fltmatch;
		gint regerr = 0;
		size_t reglen = 0;
		gchar *regmsg;
		const char *contents;
		
		contents = camel_medium_get_header (CAMEL_MEDIUM (fms->message), header);
		
		regerr = regcomp (&regexpat, match, REG_EXTENDED | REG_NEWLINE | REG_ICASE);
		if (regerr) {
			/* regerror gets called twice to get the full error string 
			   length to do proper posix error reporting */
			reglen = regerror (regerr, &regexpat, 0, 0);
			regmsg = g_malloc0 (reglen + 1);
			regerror (regerr, &regexpat, regmsg, reglen);
			camel_exception_setv (fms->ex, CAMEL_EXCEPTION_SYSTEM,
					      "Failed to perform regex search on message body: %s",
					      regmsg);
			g_free (regmsg);
			regfree (&regexpat);
		} else {
			if (contents) {
				fltmatch = g_new0 (regmatch_t, regexpat.re_nsub);
				
				if (!regexec (&regexpat, contents, regexpat.re_nsub, fltmatch, 0))
					matched = TRUE;
				
				g_free (fltmatch);
				regfree (&regexpat);
			}
		}
	}
	
	r = e_sexp_result_new (ESEXP_RES_BOOL);
	r->value.bool = matched;
	
	return r;
}

static ESExpResult *
match_all (struct _ESExp *f, int argc, struct _ESExpTerm **argv, FilterMessageSearch *fms)
{
	/* match-all: when dealing with single messages is a no-op */
	ESExpResult *r;
	
	r = e_sexp_result_new (ESEXP_RES_BOOL);
	if (argv[0]->type == ESEXP_RES_BOOL)
		r->value.bool = argv[0]->value.bool;
	else
		r->value.bool = FALSE;
	
	return r;
}

static gboolean
mime_part_matches (CamelMimePart *mime_part, const char *match, CamelException *ex)
{
	CamelStream *stream;
	GByteArray *array;
	char *text;
	regex_t regexpat;        /* regex patern */
	regmatch_t *fltmatch;
	gint regerr = 0;
	size_t reglen = 0;
	gchar *regmsg;
	gboolean matched = FALSE;
	
	array = g_byte_array_new ();
	stream = camel_stream_mem_new_with_byte_array (array);
	camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (mime_part), stream);
	camel_stream_reset (stream);
	g_byte_array_append (array, "", 1);
	
	text = array->data;
	
	regerr = regcomp (&regexpat, match, REG_EXTENDED | REG_NEWLINE | REG_ICASE);
	if (regerr) {
		/* regerror gets called twice to get the full error string 
		   length to do proper posix error reporting */
		reglen = regerror (regerr, &regexpat, 0, 0);
		regmsg = g_malloc0 (reglen + 1);
		regerror (regerr, &regexpat, regmsg, reglen);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      "Failed to perform regex search on message body: %s",
				      regmsg);
		g_free (regmsg);
		regfree (&regexpat);
	} else {
		fltmatch = g_new0 (regmatch_t, regexpat.re_nsub);
		
		if (!regexec (&regexpat, text, regexpat.re_nsub, fltmatch, 0))
			matched = TRUE;
		
		g_free (fltmatch);
		regfree (&regexpat);
	}
	
	return matched;
}

static gboolean
handle_multipart (CamelMultipart *multipart, const char *match, CamelException *ex)
{
	gboolean matched = FALSE;
	int i, nparts;
	
	nparts = camel_multipart_get_number (multipart);
	
	for (i = 0; i < nparts && !matched; i++) {
		GMimeContentField *content;
		CamelMimePart *mime_part;
		
		mime_part = camel_multipart_get_part (multipart, i);
		content = camel_mime_part_get_content_type (mime_part);
		
		if (gmime_content_field_is_type (content, "text", "*")) {
			/* we only want to match text parts */
			matched = mime_part_matches (mime_part, match, ex);
			
			if (camel_exception_is_set (ex))
				break;
		} else if (gmime_content_field_is_type (content, "multipart", "*")) {
			CamelDataWrapper *wrapper;
			CamelMultipart *mpart;
			
			wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (mime_part));
			mpart = CAMEL_MULTIPART (wrapper);
			
			matched = handle_multipart (mpart, match, ex);
			
			if (camel_exception_is_set (ex))
				break;
		}
	}
	
	return matched;
}

static ESExpResult *
body_contains (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	gboolean matched = FALSE;
	ESExpResult *r;
	
	if (argc > 0) {
		GMimeContentField *content;
		char *match;
		
		match = (*argv)->value.string;
		
		content = camel_mime_part_get_content_type (CAMEL_MIME_PART (fms->message));
		
		if (gmime_content_field_is_type (content, "multipart", "*")) {
			CamelDataWrapper *wrapper;
			CamelMultipart *multipart;
			
			wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (CAMEL_MIME_PART (fms->message)));
			multipart = CAMEL_MULTIPART (wrapper);
			
			matched = handle_multipart (multipart, match, fms->ex);
		} else {
			/* single-part message so just search the entire message */
			matched = mime_part_matches (CAMEL_MIME_PART (fms->message), match, fms->ex);
		}
	}
	
	r = e_sexp_result_new (ESEXP_RES_BOOL);
	r->value.bool = matched;
	
	return r;
}

static ESExpResult *
user_flag (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;
	gboolean truth = FALSE;
	int i;
	
	/* performs an OR of all words */
	for (i = 0; i < argc && !truth; i++) {
		if (argv[i]->type == ESEXP_RES_STRING
		    && camel_flag_get (&fms->info->user_flags, argv[i]->value.string)) {
			truth = TRUE;
			break;
		}
	}
	
	r = e_sexp_result_new (ESEXP_RES_BOOL);
	r->value.bool = truth;
	
	return r;
}

static ESExpResult *
user_tag (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;
	const char *tag;
	
	tag = camel_tag_get (&fms->info->user_tags, argv[0]->value.string);
	
	r = e_sexp_result_new (ESEXP_RES_STRING);
	r->value.string = g_strdup (tag ? tag : "");
	
	return r;
}

static ESExpResult *
get_sent_date (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;
	const char *sent_date;
	time_t date;
	
	sent_date = camel_mime_message_get_sent_date (fms->message);
	date = header_decode_date (sent_date, NULL);
	
	r = e_sexp_result_new (ESEXP_RES_INT);
	r->value.number = date;
	
	return r;
}

static ESExpResult *
get_received_date (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;
	const char *received_date;
	time_t date;
	
	received_date = camel_mime_message_get_received_date (fms->message);
	date = header_decode_date (received_date, NULL);
	
	r = e_sexp_result_new (ESEXP_RES_INT);
	r->value.number = date;
	
	return r;
}

static ESExpResult *
get_current_date (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;
	
	r = e_sexp_result_new (ESEXP_RES_INT);
	r->value.number = time (NULL);
	
	return r;
}

static ESExpResult *
get_score (struct _ESExp *f, int argc, struct _ESExpResult **argv, FilterMessageSearch *fms)
{
	ESExpResult *r;
	const char *tag;
	
	tag = camel_tag_get (&fms->info->user_tags, "score");
	
	r = e_sexp_result_new (ESEXP_RES_INT);
	if (tag)
		r->value.number = atoi (tag);
	else
		r->value.number = 0;
	
	return r;
}

gboolean
filter_message_search (CamelMimeMessage *message, CamelMessageInfo *info, const char *expression, CamelException *ex)
{
	FilterMessageSearch *fms;
	ESExp *sexp;
	ESExpResult *result;
	gboolean retval;
	int i;
	
	fms = g_new (FilterMessageSearch, 1);
	fms->message = message;
	fms->info = info;
	fms->ex = ex;
	
	sexp = e_sexp_new ();
	
	for (i = 0; i < sizeof (symbols) / sizeof (symbols[0]); i++) {
		if (symbols[i].type == 1) {
			e_sexp_add_ifunction (sexp, 0, symbols[i].name,
					      (ESExpIFunc *)symbols[i].func, fms);
		} else {
			e_sexp_add_function (sexp, 0, symbols[i].name,
					     symbols[i].func, fms);
		}
	}
	
	e_sexp_input_text (sexp, expression, strlen (expression));
	e_sexp_parse (sexp);
	result = e_sexp_eval (sexp);
	
	g_free (fms);
	
	if (result->type == ESEXP_RES_BOOL)
		retval = result->value.bool;
	else
		retval = FALSE;
	
	gtk_object_unref (GTK_OBJECT (sexp));
	e_sexp_result_free (result);
	
	return retval;
}
