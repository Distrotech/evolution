/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
 */

#ifndef MAIL_ACCOUNTS_H
#define MAIL_ACCOUNTS_H

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <gtk/gtkclist.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtkspinbutton.h>
#include <libgnomeui/gnome-color-picker.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-file-entry.h>
#include <glade/glade.h>
#include <shell/Evolution.h>

#define MAIL_ACCOUNTS_DIALOG_TYPE        (mail_accounts_dialog_get_type ())
#define MAIL_ACCOUNTS_DIALOG(o)          (GTK_CHECK_CAST ((o), MAIL_ACCOUNTS_DIALOG_TYPE, MailAccountsDialog))
#define MAIL_ACCOUNTS_DIALOG_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), MAIL_ACCOUNTS_DIALOG_TYPE, MailAccountsDialogClass))
#define IS_MAIL_ACCOUNTS_DIALOG(o)       (GTK_CHECK_TYPE ((o), MAIL_ACCOUNTS_DIALOG_TYPE))
#define IS_MAIL_ACCOUNTS_DIALOG_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), MAIL_ACCOUNTS_DIALOG_TYPE))

struct _MailAccountsDialog {
	GnomeDialog parent;
	
	GNOME_Evolution_Shell shell;
	
	GladeXML *gui;
	
	const GSList *accounts;
	gint accounts_row;
	
	/* Accounts page */
	GtkCList *mail_accounts;
	GtkButton *mail_add;
	GtkButton *mail_edit;
	GtkButton *mail_delete;
	GtkButton *mail_default;
	GtkButton *mail_able;
	
	const GSList *news;
	gint news_row;
	
	/* News page */
	GtkCList *news_accounts;
	GtkButton *news_add;
	GtkButton *news_edit;
	GtkButton *news_delete;
	
	/* Display page */
	GtkToggleButton *citation_highlight;
	GnomeColorPicker *citation_color;
	GtkToggleButton *timeout_toggle;
	GtkSpinButton *timeout;
	GtkToggleButton *images_always, *images_sometimes, *images_never;
	/*GtkToggleButton *thread_list;*/
	/*GtkToggleButton *show_preview;*/
	
	/* Composer page */
	GtkToggleButton *send_html;
	GtkOptionMenu *forward_style;
	GtkOptionMenu *charset;
	GtkToggleButton *prompt_empty_subject;
	GtkToggleButton *prompt_bcc_only;
	GtkToggleButton *prompt_unwanted_html;
	
	/* Other page */
	GtkToggleButton *empty_trash;
	GtkToggleButton *filter_log;
	GnomeFileEntry *filter_log_path;
	GtkToggleButton *confirm_expunge;
	
	/* PGP page */
	GnomeFileEntry *pgp_path;

	/* Pixmaps for the clist */
	GdkPixmap *mark_pixmap;
	GdkBitmap *mark_bitmap;
};

typedef struct _MailAccountsDialog MailAccountsDialog;

typedef struct {
	GnomeDialogClass parent_class;
	
	/* signals */
	
} MailAccountsDialogClass;

GtkType mail_accounts_dialog_get_type (void);

MailAccountsDialog *mail_accounts_dialog_new (GNOME_Evolution_Shell shell);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MAIL_ACCOUNTS_H */
