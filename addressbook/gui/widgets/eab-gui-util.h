/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* eab-gui-util.h
 * Copyright (C) 2001-2003  Ximian, Inc.
 * Author: Chris Toshok <toshok@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __E_ADDRESSBOOK_UTIL_H__
#define __E_ADDRESSBOOK_UTIL_H__

#include <gtk/gtkwindow.h>
#include "addressbook/backend/ebook/e-book.h"
#include "addressbook/gui/contact-editor/eab-contact-editor.h"
#include "addressbook/gui/contact-list-editor/e-contact-list-editor.h"

G_BEGIN_DECLS

void                eab_error_dialog              (const gchar *msg,
						   EBookStatus  status);
gint                eab_prompt_save_dialog        (GtkWindow   *parent);

EABContactEditor   *eab_show_contact_editor       (EBook       *book,
						   EContact    *contact,
						   gboolean     is_new_contact,
						   gboolean     editable);
EContactListEditor *eab_show_contact_list_editor  (EBook       *book,
						   EContact    *contact,
						   gboolean     is_new_contact,
						   gboolean     editable);
void                eab_show_multiple_cards       (EBook       *book,
						   GList       *list,
						   gboolean     editable);
void                eab_transfer_cards            (EBook       *source,
						   GList       *cards, /* adopted */
						   gboolean     delete_from_source,
						   GtkWindow   *parent_window);

#if notyet
typedef enum {
	EAB_DISPOSITION_AS_ATTACHMENT,
	EAB_DISPOSITION_AS_TO,
} EABDisposition;

void                eab_send_card                 (ECard                   *card,
						   EAddressbookDisposition  disposition);
void                eab_send_card_list            (GList                   *cards,
						   EAddressbookDisposition  disposition);
#endif

G_END_DECLS

#endif /* __E_ADDRESSBOOK_UTIL_H__ */
