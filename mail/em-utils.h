/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
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


#ifndef __EM_UTILS_H__
#define __EM_UTILS_H__

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

struct _GtkWidget;
struct _CamelFolder;
struct _CamelMimeMessage;
struct _GtkSelectionData;
struct _GtkAdjustment;

GPtrArray *em_utils_uids_copy (GPtrArray *uids);
void em_utils_uids_free (GPtrArray *uids);

gboolean em_utils_configure_account (struct _GtkWidget *parent);
gboolean em_utils_check_user_can_send_mail (struct _GtkWidget *parent);

void em_utils_edit_filters (struct _GtkWidget *parent);
void em_utils_edit_vfolders (struct _GtkWidget *parent);

void em_utils_compose_new_message (struct _GtkWidget *parent);
void em_utils_compose_new_message_with_mailto (struct _GtkWidget *parent, const char *url);

void em_utils_post_to_url (struct _GtkWidget *parent, const char *url);

void em_utils_edit_message (struct _GtkWidget *parent, struct _CamelMimeMessage *message);
void em_utils_edit_messages (struct _GtkWidget *parent, struct _CamelFolder *folder, GPtrArray *uids);

void em_utils_forward_attached (struct _GtkWidget *parent, struct _CamelFolder *folder, GPtrArray *uids);
void em_utils_forward_inline (struct _GtkWidget *parent, struct _CamelFolder *folder, GPtrArray *uids);
void em_utils_forward_quoted (struct _GtkWidget *parent, struct _CamelFolder *folder, GPtrArray *uids);

void em_utils_forward_message(struct _GtkWidget *parent, struct _CamelMimeMessage *msg);
void em_utils_forward_messages(struct _GtkWidget *parent, struct _CamelFolder *folder, GPtrArray *uids);

void em_utils_redirect_message (struct _GtkWidget *parent, struct _CamelMimeMessage *message);
void em_utils_redirect_message_by_uid (struct _GtkWidget *parent, struct _CamelFolder *folder, const char *uid);

enum {
	REPLY_MODE_SENDER,
	REPLY_MODE_ALL,
	REPLY_MODE_LIST
};

void em_utils_reply_to_message (struct _GtkWidget *parent, struct _CamelMimeMessage *message, int mode);
void em_utils_reply_to_message_by_uid (struct _GtkWidget *parent, struct _CamelFolder *folder, const char *uid, int mode);

void em_utils_post_reply_to_message_by_uid (struct _GtkWidget *parent, struct _CamelFolder *folder, const char *uid);

void em_utils_save_part(struct _GtkWidget *parent, const char *prompt, struct _CamelMimePart *part);
void em_utils_save_messages (struct _GtkWidget *parent, struct _CamelFolder *folder, GPtrArray *uids);

void em_utils_flag_for_followup (struct _GtkWidget *parent, struct _CamelFolder *folder, GPtrArray *uids);
void em_utils_flag_for_followup_clear (struct _GtkWidget *parent, struct _CamelFolder *folder, GPtrArray *uids);
void em_utils_flag_for_followup_completed (struct _GtkWidget *parent, struct _CamelFolder *folder, GPtrArray *uids);

void em_utils_selection_set_mailbox(struct _GtkSelectionData *data, struct _CamelFolder *folder, GPtrArray *uids);
void em_utils_selection_get_mailbox(struct _GtkSelectionData *data, struct _CamelFolder *folder);
/* FIXME: be nice if these also worked on struct _CamelFolder's, no easy way to get uri from folder yet tho */
void em_utils_selection_set_uidlist(struct _GtkSelectionData *data, const char *uri, GPtrArray *uids);
int  em_utils_selection_get_uidlist(struct _GtkSelectionData *data, char **uri, GPtrArray **uidsp);
void em_utils_selection_set_urilist(struct _GtkSelectionData *data, struct _CamelFolder *folder, GPtrArray *uids);

char *em_utils_temp_save_part(struct _GtkWidget *parent, struct _CamelMimePart *part);

gboolean em_utils_folder_is_drafts(struct _CamelFolder *folder, const char *uri);
gboolean em_utils_folder_is_sent(struct _CamelFolder *folder, const char *uri);
gboolean em_utils_folder_is_outbox(struct _CamelFolder *folder, const char *uri);

void em_utils_adjustment_page(struct _GtkAdjustment *adj, gboolean down);


char *em_utils_quote_message (CamelMimeMessage *message, const char *credits);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EM_UTILS_H__ */
