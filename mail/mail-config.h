/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  Copyright 2000, Helix Code, Inc. (http://www.helixcode.com)
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
 */

#ifndef _MAIL_CONFIG_H
#define _MAIL_CONFIG_H



#include <glib.h>

typedef struct 
{
	gchar *name;
	gchar *address;
	gchar *org;
	gchar *sig;
} MailConfigIdentity;

typedef struct
{
	gchar *url;
	gboolean keep_on_server;
} MailConfigService;

typedef struct 
{
	gboolean configured;
	GSList *ids;
	GSList *sources;
	GSList *news;
	MailConfigService *transport;
	gboolean send_html;
	gboolean thread_list;
	gint paned_size;
} MailConfig;

/* Identities */
MailConfigIdentity *identity_copy (MailConfigIdentity *id);
void identity_destroy (MailConfigIdentity *id);
void identity_destroy_each (gpointer item, gpointer data);

/* Services */
MailConfigService *service_copy (MailConfigService *source);
void service_destroy (MailConfigService *source);
void service_destroy_each (gpointer item, gpointer data);

/* Configuration */
void mail_config_init (void);
void mail_config_clear (void);
void mail_config_read (void);
void mail_config_write (void);
void mail_config_write_on_exit (void);

/* General Accessor functions */
gboolean       mail_config_is_configured   (void);
MailConfig     *mail_config_fetch          (void);
gboolean       mail_config_send_html       (void);
gboolean       mail_config_thread_list     (void);
gint           mail_config_paned_size      (void);
void           mail_config_set_thread_list (gboolean value);
void           mail_config_set_paned_size  (gint size);

/* Identity Accessor functions */
MailConfigIdentity *mail_config_get_default_identity (void);
GSList             *mail_config_get_identities       (void);

/* Service Accessor functions */
MailConfigService *mail_config_get_default_source (void);
MailConfigService *mail_config_get_transport      (void);

#endif


