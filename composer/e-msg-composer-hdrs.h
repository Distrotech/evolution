/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* msg-composer-hdrs.h
 *
 * Copyright (C) 1999 Ximian, Inc.
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
 * Author: Ettore Perazzoli
 */

#ifndef ___E_MSG_COMPOSER_HDRS_H__
#define ___E_MSG_COMPOSER_HDRS_H__

#include <gtk/gtktable.h>
#include <camel/camel-mime-message.h>
#include <addressbook/backend/ebook/e-destination.h>
#include <mail/mail-config.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E_TYPE_MSG_COMPOSER_HDRS		(e_msg_composer_hdrs_get_type ())
#define E_MSG_COMPOSER_HDRS(obj)		(GTK_CHECK_CAST ((obj), E_TYPE_MSG_COMPOSER_HDRS, EMsgComposerHdrs))
#define E_MSG_COMPOSER_HDRS_CLASS(klass)	(GTK_CHECK_CLASS_CAST ((klass), E_TYPE_MSG_COMPOSER_HDRS, EMsgComposerHdrsClass))
#define E_IS_MSG_COMPOSER_HDRS(obj)		(GTK_CHECK_TYPE ((obj), E_TYPE_MSG_COMPOSER_HDRS))
#define E_IS_MSG_COMPOSER_HDRS_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((obj), E_TYPE_MSG_COMPOSER_HDRS))


typedef struct _EMsgComposerHdrs        EMsgComposerHdrs;
typedef struct _EMsgComposerHdrsClass   EMsgComposerHdrsClass;
typedef struct _EMsgComposerHdrsPrivate EMsgComposerHdrsPrivate;

struct _EMsgComposerHdrs {
	GtkTable parent;

	EMsgComposerHdrsPrivate *priv;
	
	const MailConfigAccount *account;
	
	gboolean has_changed;
};

struct _EMsgComposerHdrsClass {
	GtkTableClass parent_class;

	void (* show_address_dialog) (EMsgComposerHdrs *hdrs);

	void (* subject_changed) (EMsgComposerHdrs *hdrs, gchar *subject);

	void (* hdrs_changed) (EMsgComposerHdrs *hdrs);

	void (* from_changed) (EMsgComposerHdrs *hdrs);
};

typedef enum {
	E_MSG_COMPOSER_VISIBLE_FROM    = 1,
	E_MSG_COMPOSER_VISIBLE_REPLYTO = 2,
	E_MSG_COMPOSER_VISIBLE_CC      = 4,
	E_MSG_COMPOSER_VISIBLE_BCC     = 8,
	E_MSG_COMPOSER_VISIBLE_SUBJECT = 16
} EMsgComposerHeaderVisibleFlags;


GtkType     e_msg_composer_hdrs_get_type           (void);
GtkWidget  *e_msg_composer_hdrs_new                (gint visible_flags);

void        e_msg_composer_hdrs_to_message         (EMsgComposerHdrs *hdrs,
						    CamelMimeMessage *msg);

void        e_msg_composer_hdrs_set_from_account   (EMsgComposerHdrs *hdrs,
						    const char *account_name);
void        e_msg_composer_hdrs_set_reply_to       (EMsgComposerHdrs *hdrs,
						    const char *reply_to);
void        e_msg_composer_hdrs_set_to             (EMsgComposerHdrs *hdrs,
						    EDestination    **to_destv);
void        e_msg_composer_hdrs_set_cc             (EMsgComposerHdrs *hdrs,
						    EDestination    **cc_destv);
void        e_msg_composer_hdrs_set_bcc            (EMsgComposerHdrs *hdrs,
						    EDestination    **bcc_destv);
void        e_msg_composer_hdrs_set_subject        (EMsgComposerHdrs *hdrs,
						    const char       *subject);

CamelInternetAddress *e_msg_composer_hdrs_get_from (EMsgComposerHdrs *hdrs);
CamelInternetAddress *e_msg_composer_hdrs_get_reply_to (EMsgComposerHdrs *hdrs);

EDestination **e_msg_composer_hdrs_get_to          (EMsgComposerHdrs *hdrs);
EDestination **e_msg_composer_hdrs_get_cc          (EMsgComposerHdrs *hdrs);
EDestination **e_msg_composer_hdrs_get_bcc         (EMsgComposerHdrs *hdrs);
EDestination **e_msg_composer_hdrs_get_recipients  (EMsgComposerHdrs *hdrs);
char          *e_msg_composer_hdrs_get_subject     (EMsgComposerHdrs *hdrs);

GtkWidget  *e_msg_composer_hdrs_get_reply_to_entry (EMsgComposerHdrs *hdrs);
GtkWidget  *e_msg_composer_hdrs_get_to_entry       (EMsgComposerHdrs *hdrs);
GtkWidget  *e_msg_composer_hdrs_get_cc_entry       (EMsgComposerHdrs *hdrs);
GtkWidget  *e_msg_composer_hdrs_get_bcc_entry      (EMsgComposerHdrs *hdrs);
GtkWidget  *e_msg_composer_hdrs_get_subject_entry  (EMsgComposerHdrs *hdrs);

gint        e_msg_composer_get_hdrs_visible        (EMsgComposerHdrs *hdrs);
void        e_msg_composer_set_hdrs_visible        (EMsgComposerHdrs *hdrs,
						    gint flags);

#ifdef _cplusplus
}
#endif /* _cplusplus */


#endif /* __E_MSG_COMPOSER_HDRS_H__ */
