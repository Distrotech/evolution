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

#include <gtk/gtk.h>

#include <camel/camel-folder.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

GPtrArray *em_utils_uids_copy (GPtrArray *uids);
void em_utils_uids_free (GPtrArray *uids);

gboolean em_utils_configure_account (GtkWidget *parent);
gboolean em_utils_check_user_can_send_mail (GtkWidget *parent);

void em_utils_edit_filters (GtkWidget *parent);
void em_utils_edit_vfolders (GtkWidget *parent);

void em_utils_compose_new_message (GtkWidget *parent);
void em_utils_compose_new_message_with_mailto (GtkWidget *parent, const char *url);

void em_utils_post_to_url (GtkWidget *parent, const char *url);

void em_utils_edit_message (GtkWidget *parent, CamelMimeMessage *message);
void em_utils_edit_messages (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids);

void em_utils_forward_attached (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids);
void em_utils_forward_inline (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids);
void em_utils_forward_quoted (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids);

void em_utils_redirect_message (GtkWidget *parent, CamelMimeMessage *message);
void em_utils_redirect_message_by_uid (GtkWidget *parent, CamelFolder *folder, const char *uid);

enum {
	REPLY_MODE_SENDER,
	REPLY_MODE_ALL,
	REPLY_MODE_LIST
};

void em_utils_reply_to_message (GtkWidget *parent, CamelMimeMessage *message, int mode);
void em_utils_reply_to_message_by_uid (GtkWidget *parent, CamelFolder *folder, const char *uid, int mode);

void em_utils_post_reply_to_message_by_uid (GtkWidget *parent, CamelFolder *folder, const char *uid);

void em_utils_save_message (GtkWidget *parent, CamelMimeMessage *message);
void em_utils_save_messages (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids);

void em_utils_flag_for_followup (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids);
void em_utils_flag_for_followup_clear (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids);
void em_utils_flag_for_followup_completed (GtkWidget *parent, CamelFolder *folder, GPtrArray *uids);

void em_utils_selection_set_mailbox(GtkSelectionData *data, CamelFolder *folder, GPtrArray *uids);
void em_utils_selection_get_mailbox(GtkSelectionData *data, CamelFolder *folder);
/* FIXME: be nice if these also worked on CamelFolder's, no easy way to get uri from folder yet tho */
void em_utils_selection_set_uidlist(GtkSelectionData *data, const char *uri, GPtrArray *uids);
int  em_utils_selection_get_uidlist(GtkSelectionData *data, char **uri, GPtrArray **uidsp);
void em_utils_selection_set_urilist(GtkSelectionData *data, CamelFolder *folder, GPtrArray *uids);

gboolean em_utils_folder_is_drafts(CamelFolder *folder, const char *uri);
gboolean em_utils_folder_is_sent(CamelFolder *folder, const char *uri);
gboolean em_utils_folder_is_outbox(CamelFolder *folder, const char *uri);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EM_UTILS_H__ */
