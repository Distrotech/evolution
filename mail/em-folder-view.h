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


#ifndef _EM_FOLDER_VIEW_H
#define _EM_FOLDER_VIEW_H

#include <gtk/gtkvbox.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

struct _MessageList;
struct _EMFormatHTMLDisplay;
struct _CamelFolder;
struct _CamelMedium;

typedef struct _EMFolderView EMFolderView;
typedef struct _EMFolderViewClass EMFolderViewClass;

struct _EMFolderView {
	GtkVBox parent;

	struct _MessageList *list;
	struct _EMFormatHTMLDisplay *preview;

	struct _CamelFolder *folder;
	char *folder_uri;

	struct _EMFolderViewPrivate *priv;

	/* used to load ui from base activate implementation */
	GSList *ui_files;	/* const char * list */
	const char *ui_app_name;

	int preview_active:1;
};

struct _EMFolderViewClass {
	GtkVBoxClass parent_class;

	/* if used as a control, used to activate/deactivate custom menu's */
	void (*activate)(EMFolderView *, struct _BonoboUIComponent *uic, int state);

	void (*set_folder_uri)(EMFolderView *emfv, const char *uri);
	void (*set_folder)(EMFolderView *emfv, struct _CamelFolder *folder, const char *uri);
	void (*set_message)(EMFolderView *emfv, const char *uid);
};

GType em_folder_view_get_type(void);

GtkWidget *em_folder_view_new(void);

#define em_folder_view_activate(emfv, uic, state) ((EMFolderViewClass *)G_OBJECT_GET_CLASS(emfv))->activate((emfv), (uic), (state))
#define em_folder_view_set_folder(emfv, folder, uri) ((EMFolderViewClass *)G_OBJECT_GET_CLASS(emfv))->set_folder((emfv), (folder), (uri))
#define em_folder_view_set_folder_uri(emfv, uri) ((EMFolderViewClass *)G_OBJECT_GET_CLASS(emfv))->set_folder_uri((emfv), (uri))
#define em_folder_view_set_message(emfv, uid) ((EMFolderViewClass *)G_OBJECT_GET_CLASS(emfv))->set_message((emfv), (uid))

int em_folder_view_mark_selected(EMFolderView *emfv, guint32 mask, guint32 set);
int em_folder_view_open_selected(EMFolderView *emfv);

int em_folder_view_print(EMFolderView *emfv, int preview);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! _EM_FOLDER_VIEW_H */
