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
 *		Michael Zucchi <notzed@ximian.com>
 *		Ettore Perazzoli <ettore@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef _MAIL_AUTOFILTER_H
#define _MAIL_AUTOFILTER_H

struct _FilterRule;
struct _EMVFolderContext;
struct _EMFilterContext;
struct _CamelMimeMessage;
struct _CamelInternetAddress;
struct _CamelStore;

enum {
	AUTO_SUBJECT = 1,
	AUTO_FROM = 2,
	AUTO_TO = 4,
	AUTO_MLIST = 8
};

struct _FilterRule *em_vfolder_rule_from_message(struct _EMVFolderContext *context, struct _CamelMimeMessage *msg, gint flags, const gchar *source);
struct _FilterRule *filter_rule_from_message(struct _EMFilterContext *context, struct _CamelMimeMessage *msg, gint flags);
struct _FilterRule *em_vfolder_rule_from_address(struct _EMVFolderContext *context, struct _CamelInternetAddress *addr, gint flags, const gchar *source);

/* easiest place to put this */
void  filter_gui_add_from_message(struct _CamelMimeMessage *msg, const gchar *source, gint flags);

/* Also easiest place for these, we should really share a global rule context for this stuff ... */
void mail_filter_rename_uri(struct _CamelStore *store, const gchar *olduri, const gchar *newuri);
void mail_filter_delete_uri(struct _CamelStore *store, const gchar *uri);

#endif
