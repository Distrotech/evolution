/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-minicard-view-widget.h
 * Copyright (C) 2000  Ximian, Inc.
 * Author: Chris Lahey <clahey@ximian.com>
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
#ifndef __E_ADDRESSBOOK_SEARCH_DIALOG_H__
#define __E_ADDRESSBOOK_SEARCH_DIALOG_H__

#include <ebook/e-book.h>

#include "addressbook/gui/widgets/e-addressbook-view.h"
#include "filter/rule-context.h"
#include "filter/filter-rule.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <libgnomeui/gnome-dialog.h>

#define E_ADDRESSBOOK_SEARCH_DIALOG_TYPE			(e_addressbook_search_dialog_get_type ())
#define E_ADDRESSBOOK_SEARCH_DIALOG(obj)			(GTK_CHECK_CAST ((obj), E_ADDRESSBOOK_SEARCH_DIALOG_TYPE, EAddressbookSearchDialog))
#define E_ADDRESSBOOK_SEARCH_DIALOG_CLASS(klass)		(GTK_CHECK_CLASS_CAST ((klass), E_ADDRESSBOOK_SEARCH_DIALOG_TYPE, EAddressbookSearchDialogClass))
#define E_IS_ADDRESSBOOK_SEARCH_DIALOG(obj) 		(GTK_CHECK_TYPE ((obj), E_ADDRESSBOOK_SEARCH_DIALOG_TYPE))
#define E_IS_ADDRESSBOOK_SEARCH_DIALOG_CLASS(klass) 	(GTK_CHECK_CLASS_TYPE ((obj), E_ADDRESSBOOK_SEARCH_DIALOG_TYPE))


typedef struct _EAddressbookSearchDialog       EAddressbookSearchDialog;
typedef struct _EAddressbookSearchDialogClass  EAddressbookSearchDialogClass;

struct _EAddressbookSearchDialog
{
	GnomeDialog parent;

	GtkWidget *search;

	EAddressbookView *view;

	RuleContext *context;
	FilterRule *rule;
};

struct _EAddressbookSearchDialogClass
{
	GnomeDialogClass parent_class;
};

GtkType    e_addressbook_search_dialog_get_type (void);

GtkWidget *e_addressbook_search_dialog_new (EAddressbookView *view);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E_ADDRESSBOOK_SEARCH_DIALOG_H__ */
