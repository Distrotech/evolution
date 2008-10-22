/*
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
 *		Srinivasa Ragavan <sragavan@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef EXCHANGE_ACCOUNT_LISTENER_H
#define EXCHANGE_ACCOUNT_LISTENER_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define EXCHANGE_TYPE_ACCOUNT_LISTENER            (exchange_account_listener_get_type ())
#define EXCHANGE_ACCOUNT_LISTENER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_ACCOUNT_LISTENER, ExchangeAccountListener))
#define EXCHANGE_ACCOUNT_LISTENER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_ACCOUNT_LISTENER,  ExchangeAccountListenerClass))
#define EXCHANGE_IS_ACCOUNT_LISTENER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_ACCOUNT_LISTENER))
#define EXCHANGE_IS_ACCOUNT_LISTENER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_ACCOUNT_LISTENER))

typedef struct _ExchangeAccountListener ExchangeAccountListener;
typedef struct _ExchangeAccountListenerClass ExchangeAccountListenerClass;
typedef struct _ExchangeAccountListenerPrivate ExchangeAccountListenerPrivate;

struct _ExchangeAccountListener {
       GObject parent;
       ExchangeAccountListenerPrivate *priv;
};

struct _ExchangeAccountListenerClass {
       GObjectClass parent_class;
};

GType                   exchange_account_listener_get_type (void);
ExchangeAccountListener *exchange_account_listener_new (void);
GSList 			*exchange_account_listener_peek_folder_list (void); 
void 			exchange_account_listener_get_folder_list (void);
void                    exchange_account_listener_free_folder_list (void);

G_END_DECLS

#endif
