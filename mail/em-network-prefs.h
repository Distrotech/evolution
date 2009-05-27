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
 *		Veerapuram Varadhan  <vvaradhan@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef __EM_NETWORK_PREFS_H__
#define __EM_NETWORK_PREFS_H__

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <gtk/gtk.h>

#define EM_NETWORK_PREFS_TYPE        (em_network_prefs_get_type ())
#define EM_NETWORK_PREFS(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), EM_NETWORK_PREFS_TYPE, EMNetworkPrefs))
#define EM_NETWORK_PREFS_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), EM_NETWORK_PREFS_TYPE, EMNetworkPrefsClass))
#define EM_IS_NETWORK_PREFS(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), EM_NETWORK_PREFS_TYPE))
#define EM_IS_NETWORK_PREFS_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), EM_NETWORK_PREFS_TYPE))

typedef struct _EMNetworkPrefs EMNetworkPrefs;
typedef struct _EMNetworkPrefsClass EMNetworkPrefsClass;

struct _GtkToggleButton;
struct _GtkEntry;
struct _GladeXML;
struct _GConfClient;

typedef enum {
	NETWORK_PROXY_SYS_SETTINGS,
	NETWORK_PROXY_DIRECT_CONNECTION,
	NETWORK_PROXY_MANUAL,
	NETWORK_PROXY_AUTOCONFIG
} NetworkConfigProxyType;


struct _EMNetworkPrefs {
	GtkVBox parent_object;

	struct _GConfClient *gconf;

	struct _GladeXML *gui;

	/* Default Behavior */
	struct _GtkToggleButton *sys_proxy;
	struct _GtkToggleButton *no_proxy;
	struct _GtkToggleButton *manual_proxy;
#if 0
	struct _GtkToggleButton *auto_proxy;
#endif
	struct _GtkToggleButton *use_auth;

	struct _GtkEntry *http_host;
	struct _GtkEntry *https_host;
	struct _GtkEntry *ignore_hosts;
#if 0
	struct _GtkEntry *auto_proxy_url;
#endif
	struct _GtkEntry *auth_user;
	struct _GtkEntry *auth_pwd;

	struct _GtkLabel *lbl_http_host;
	struct _GtkLabel *lbl_http_port;
	struct _GtkLabel *lbl_https_host;
	struct _GtkLabel *lbl_https_port;
	struct _GtkLabel *lbl_ignore_hosts;
	struct _GtkLabel *lbl_auth_user;
	struct _GtkLabel *lbl_auth_pwd;

	struct _GtkSpinButton *http_port;
	struct _GtkSpinButton *https_port;
#if 0
	struct _GtkLabel *lbl_socks_host;
	struct _GtkEntry *socks_host;
	struct _GtkLabel *lbl_socks_port;
	struct _GtkSpinButton *socks_port;
#endif
};

struct _EMNetworkPrefsClass {
	GtkVBoxClass parent_class;

	/* signals */

};

GType em_network_prefs_get_type (void);

struct _GtkWidget *em_network_prefs_new (void);

/* needed by global config */
#define EM_NETWORK_PREFS_CONTROL_ID "OAFIID:GNOME_Evolution_Mail_NetworkPrefs_ConfigControl:" BASE_VERSION

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EM_NETWORK_PREFS_H__ */
