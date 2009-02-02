/*
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>  
 *
 *
 * Authors:
 *		Michel Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef __EM_EVENT_H__
#define __EM_EVENT_H__

#include <glib-object.h>

#include "e-util/e-event.h"
#include "mail/em-folder-browser.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

struct _CamelFolder;
struct _CamelMimeMessage;
struct _EMsgComposer;
struct _GtkWidget;

typedef struct _EMEvent EMEvent;
typedef struct _EMEventClass EMEventClass;

/* Current target description */
enum _em_event_target_t {
	EM_EVENT_TARGET_FOLDER,
	EM_EVENT_TARGET_FOLDER_BROWSER,
	EM_EVENT_TARGET_MESSAGE,
	EM_EVENT_TARGET_COMPOSER,
	EM_EVENT_TARGET_SEND_RECEIVE,
	EM_EVENT_TARGET_CUSTOM_ICON,
};

/* Flags for FOLDER BROWSER Events*/
enum {
	EM_EVENT_FOLDER_BROWSER = 1<< 0,
};

/* Flags that describe TARGET_FOLDER */
enum {
	EM_EVENT_FOLDER_NEWMAIL = 1<< 0,
};

/* Flags that describe TARGET_MESSAGE */
enum {
	EM_EVENT_MESSAGE_REPLY_ALL = 1<< 0,
};

/* Flags that describe TARGET_COMPOSER */
enum {
	EM_EVENT_COMPOSER_SEND_OPTION = 1<< 0,
};

/* Flags that describe TARGET_SEND_RECEIVE*/
enum {
	EM_EVENT_SEND_RECEIVE = 1<< 0,
};

/* Flags that describe TARGET_CUSTOM_ICON*/
enum {
	EM_EVENT_CUSTOM_ICON = 1<< 0,
};

typedef struct _EMEventTargetFolder EMEventTargetFolder;

struct _EMEventTargetFolder {
	EEventTarget target;
	char *uri;
	guint  new;
	gboolean is_inbox;
	char *name;
};

typedef struct _EMEventTargetMessage EMEventTargetMessage;

struct _EMEventTargetMessage {
	EEventTarget              target;
	struct _CamelFolder      *folder;
	char                     *uid;
	struct _CamelMimeMessage *message;
};

typedef struct _EMEventTargetComposer EMEventTargetComposer;

struct _EMEventTargetComposer {
	EEventTarget target;

	struct _EMsgComposer *composer;
};

typedef struct _EMEventTargetFolderBrowser EMEventTargetFolderBrowser;

struct _EMEventTargetFolderBrowser {
	EEventTarget target;

	struct _EMFolderBrowser *emfb;
};


typedef struct _EMEventTargetSendReceive EMEventTargetSendReceive;

struct _EMEventTargetSendReceive {
	EEventTarget target;

	struct _GtkWidget *table;
	gpointer data;
	int row;
};

typedef struct _EMEventTargetCustomIcon EMEventTargetCustomIcon;

struct _EMEventTargetCustomIcon {
	EEventTarget target;

	GtkCellRenderer *renderer;
	const char 	*folder_name;
};

typedef struct _EEventItem EMEventItem;

/* The object */
struct _EMEvent {
	EEvent popup;

	struct _EMEventPrivate *priv;
};

struct _EMEventClass {
	EEventClass popup_class;
};

GType em_event_get_type(void);

EMEvent *em_event_peek(void);

EMEventTargetFolder *em_event_target_new_folder(EMEvent *emp, const char *uri, guint32 flags);
EMEventTargetFolderBrowser *em_event_target_new_folder_browser (EMEvent *eme, EMFolderBrowser *emfb);
EMEventTargetComposer *em_event_target_new_composer(EMEvent *emp, const struct _EMsgComposer *composer, guint32 flags);
EMEventTargetMessage *em_event_target_new_message(EMEvent *emp, struct _CamelFolder *folder, struct _CamelMimeMessage *message, const char *uid, guint32 flags);
EMEventTargetSendReceive * em_event_target_new_send_receive(EMEvent *eme, struct _GtkWidget *table, gpointer data, int row, guint32 flags);
EMEventTargetCustomIcon * em_event_target_new_custom_icon(EMEvent *eme, GtkCellRenderer *renderer, const char *uri, guint32 flags);

/* ********************************************************************** */

typedef struct _EMEventHook EMEventHook;
typedef struct _EMEventHookClass EMEventHookClass;

struct _EMEventHook {
	EEventHook hook;
};

struct _EMEventHookClass {
	EEventHookClass hook_class;
};

GType em_event_hook_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EM_EVENT_H__ */
