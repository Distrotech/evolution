/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Authors: Michel Zucchi <notzed@ximian.com>
 *
 *  Copyright 2004 Novell Inc. (www.ximian.com)
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
  This a bit 'whipped together', so is likely to change mid-term
*/

#ifndef __E_EVENT_H__
#define __E_EVENT_H__

#include <glib-object.h>
#include "e-util/e-msgport.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

/* This is an abstract event management class. */

typedef struct _EEvent EEvent;
typedef struct _EEventClass EEventClass;

typedef struct _EEventItem EEventItem;
typedef struct _EEventFactory EEventFactory; /* anonymous type */
typedef struct _EEventTarget EEventTarget;

typedef void (*EEventFreeFunc)(GSList *items, void *data);
typedef void (*EEventFunc)(EEventItem *item, EEventTarget *target, void *data);
typedef void (*EEventFactoryFunc)(EEvent *ee, EEventTarget *target, void *);

/* event type, not sure we need this */
enum _e_event_t {
	E_EVENT_PASS,		/* passthrough */
	E_EVENT_SINK,		/* sink events */
};

struct _EEventItem {
	enum _e_event_t type;
	int priority;		/* priority of event */
	const char *id;		/* event id */
	int target_type;
	EEventFunc handle;
	void *handle_data;
	guint32 enable;		/* enable mask */
	EEvent *event;		/* parent, set by eevent */
};

/* base event target type */
struct _EEventTarget {
	struct _EEvent *event;	/* used for virtual methods */

	guint32 type;		/* targe type, for implementors */
	guint32 mask;		/* depends on type, enable mask */

	/* implementation fields follow */
};

/* The object */
struct _EEvent {
	GObject object;

	struct _EEventPrivate *priv;

	char *id;

	EEventTarget *target;
};

struct _EEventClass {
	GObjectClass object_class;

	void (*target_free)(EEvent *ep, EEventTarget *t);
};

GType e_event_get_type(void);

EEvent *e_event_construct(EEvent *, const char *id);

void e_event_add_items(EEvent *emp, GSList *items, EEventFreeFunc freefunc, void *data);

void e_event_emit(EEvent *, const char *id, EEventTarget *);

void *e_event_target_new(EEvent *, int type, size_t size);
void e_event_target_free(EEvent *, void *);

/* ********************************************************************** */

/* event plugin target, they are closely integrated */

/* To implement a basic event menu plugin, you just need to subclass
   this and initialise the class target type tables */

/* For events, the plugin item talks to a specific instance, rather than
   a set of instances of the hook handler */

#include "e-util/e-plugin.h"

typedef struct _EEventHookItem EEventHookItem;
typedef struct _EEventHookGroup EEventHookGroup;
typedef struct _EEventHook EEventHook;
typedef struct _EEventHookClass EEventHookClass;

typedef struct _EPluginHookTargetMap EEventHookTargetMap;
typedef struct _EPluginHookTargetKey EEventHookTargetMask;

typedef void (*EEventHookFunc)(struct _EPlugin *plugin, EEventTarget *target);

struct _EEventHookItem {
	EEventItem item;

	struct _EEventHook *hook; /* parent pointer */
	char *handle;		/* activate handler */
};

struct _EEventHook {
	EPluginHook hook;
};

struct _EEventHookClass {
	EPluginHookClass hook_class;

	/* EEventHookTargetMap by .type */
	GHashTable *target_map;
	/* the event router these events's belong to */
	EEvent *event;
};

GType e_event_hook_get_type(void);

/* for implementors */
void e_event_hook_class_add_target_map(EEventHookClass *klass, const EEventHookTargetMap *);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E_EVENT_H__ */
