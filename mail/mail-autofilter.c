/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* mail-autofilter.c
 *
 * Copyright (C) 2000  Ximian, Inc.
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
 * Authors:
 *   Michael Zucchi <notzed@ximian.com>
 *   Ettore Perazzoli <ettore@ximian.com>
 */

/* Code for autogenerating rules or filters from a message.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>

#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-app.h>
#include <libgnomeui/gnome-app-helper.h>
#include <libgnomeui/gnome-popup-menu.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <libgnomeui/gnome-stock.h>
#include <gal/util/e-unicode-i18n.h>

#include "mail-vfolder.h"
#include "mail-autofilter.h"

#include "camel/camel.h"

#include "filter/vfolder-context.h"
#include "filter/vfolder-rule.h"
#include "filter/vfolder-editor.h"

#include "filter/filter-context.h"
#include "filter/filter-filter.h"
#include "filter/filter-editor.h"
#include "filter/filter-option.h"

extern char *evolution_dir;

static void
rule_match_recipients (RuleContext *context, FilterRule *rule, CamelInternetAddress *iaddr)
{
	FilterPart *part;
	FilterElement *element;
	int i;
	const char *real, *addr;
	char *namestr;
	
	/* address types etc should handle multiple values */
	for (i = 0; camel_internet_address_get (iaddr, i, &real, &addr); i++) {
		part = rule_context_create_part (context, "to");
		filter_rule_add_part ((FilterRule *)rule, part);
		element = filter_part_find_element (part, "recipient-type");
		filter_option_set_current ((FilterOption *)element, "contains");
		element = filter_part_find_element (part, "recipient");
		filter_input_set_value ((FilterInput *)element, addr);
		
		namestr = g_strdup_printf (U_("Mail to %s"), real && real[0] ? real : addr);
		filter_rule_set_name (rule, namestr);
		g_free (namestr);
	}
}


/* remove 're' part of a subject */
static const char *
strip_re (const char *subject)
{
	const unsigned char *s, *p;
	
	s = (unsigned char *) subject;
	
	while (*s) {
		while (isspace (*s))
			s++;
		if (s[0] == 0)
			break;
		if ((s[0] == 'r' || s[0] == 'R')
		    && (s[1] == 'e' || s[1] == 'E')) {
			p = s+2;
			while (isdigit (*p) || (ispunct (*p) && (*p != ':')))
				p++;
			if (*p == ':') {
				s = p + 1;
			} else
				break;
		} else
			break;
	}
	
	return (char *) s;
}

#if 0
int
reg_match (char *str, char *regstr)
{
	regex_t reg;
	int error;
	int ret;

	error = regcomp(&reg, regstr, REG_EXTENDED|REG_ICASE|REG_NOSUB);
	if (error != 0) {
		return 0;
	}
	error = regexec(&reg, str, 0, NULL, 0);
	regfree(&reg);
	return (error == 0);
}
#endif

static void
rule_add_subject (RuleContext *context, FilterRule *rule, const char *text)
{
	FilterPart *part;
	FilterElement *element;
	
	/* dont match on empty strings ever */
	if (*text == 0)
		return;
	part = rule_context_create_part (context, "subject");
	filter_rule_add_part ((FilterRule *)rule, part);
	element = filter_part_find_element (part, "subject-type");
	filter_option_set_current ((FilterOption *)element, "contains");
	element = filter_part_find_element (part, "subject");
	filter_input_set_value ((FilterInput *)element, text);
}

static void
rule_add_sender (RuleContext *context, FilterRule *rule, const char *text)
{
	FilterPart *part;
	FilterElement *element;
	
	/* dont match on empty strings ever */
	if (*text == 0)
		return;
	part = rule_context_create_part (context, "sender");
	filter_rule_add_part ((FilterRule *)rule, part);
	element = filter_part_find_element (part, "sender-type");
	filter_option_set_current ((FilterOption *)element, "contains");
	element = filter_part_find_element (part, "sender");
	filter_input_set_value ((FilterInput *)element, text);
}

/* do a bunch of things on the subject to try and detect mailing lists, remove
   unneeded stuff, etc */
static void
rule_match_subject (RuleContext *context, FilterRule *rule, const char *subject)
{
	const char *s;
	const char *s1, *s2;
	char *tmp;
	
	s = strip_re (subject);
	/* dont match on empty subject */
	if (*s == 0)
		return;
	
	/* [blahblah] is probably a mailing list, match on it separately */
	s1 = strchr (s, '[');
	s2 = strchr (s, ']');
	if (s1 && s2 && s1 < s2) {
		/* probably a mailing list, match on the mailing list name */
		tmp = alloca (s2 - s1 + 2);
		memcpy (tmp, s1, s2 - s1 + 1);
		tmp[s2 - s1 + 1] = 0;
		g_strstrip (tmp);
		rule_add_subject (context, rule, tmp);
		s = s2 + 1;
	}
	/* Froblah: at the start is probably something important (e.g. bug number) */
	s1 = strchr (s, ':');
	if (s1) {
		tmp = alloca (s1 - s + 1);
		memcpy (tmp, s, s1-s);
		tmp[s1 - s] = 0;
		g_strstrip (tmp);
		rule_add_subject (context, rule, tmp);
		s = s1+1;
	}
	
	/* just lump the rest together */
	tmp = alloca (strlen (s) + 1);
	strcpy (tmp, s);
	g_strstrip (tmp);
	rule_add_subject (context, rule, tmp);
}

static void
rule_match_mlist(RuleContext *context, FilterRule *rule, const char *mlist)
{
	FilterPart *part;
	FilterElement *element;

	if (mlist[0] == 0)
		return;

	part = rule_context_create_part(context, "mlist");
	filter_rule_add_part(rule, part);
	
	element = filter_part_find_element(part, "mlist-type");
	filter_option_set_current((FilterOption *)element, "is");
	
	element = filter_part_find_element (part, "mlist");
	filter_input_set_value((FilterInput *)element, mlist);
}

static void
rule_from_message (FilterRule *rule, RuleContext *context, CamelMimeMessage *msg, int flags)
{
	CamelInternetAddress *addr;
	
	rule->grouping = FILTER_GROUP_ANY;
	
	if (flags & AUTO_SUBJECT) {
		const char *subject = msg->subject ? msg->subject : "";
		char *namestr;
		
		rule_match_subject (context, rule, subject);
		
		namestr = g_strdup_printf (U_("Subject is %s"), strip_re (subject));
		filter_rule_set_name (rule, namestr);
		g_free (namestr);
	}
	/* should parse the from address into an internet address? */
	if (flags & AUTO_FROM) {
		const CamelInternetAddress *from;
		int i;
		const char *name, *addr;
		char *namestr;

		from = camel_mime_message_get_from(msg);
		for (i=0;camel_internet_address_get(from, i, &name, &addr); i++) {
			rule_add_sender(context, rule, addr);
			if (name==NULL || name[0]==0)
				name = addr;
			namestr = g_strdup_printf(U_("Mail from %s"), name);
			filter_rule_set_name(rule, namestr);
			g_free(namestr);
		}
	}
	if (flags & AUTO_TO) {
		addr = (CamelInternetAddress *)camel_mime_message_get_recipients (msg, CAMEL_RECIPIENT_TYPE_TO);
		rule_match_recipients (context, rule, addr);
		addr = (CamelInternetAddress *)camel_mime_message_get_recipients (msg, CAMEL_RECIPIENT_TYPE_CC);
		rule_match_recipients (context, rule, addr);
	}
	if (flags & AUTO_MLIST) {
		char *name, *mlist;

		mlist = header_raw_check_mailing_list(&((CamelMimePart *)msg)->headers);
		if (mlist) {
			rule_match_mlist(context, rule, mlist);
			name = g_strdup_printf(U_("%s mailing list"), mlist);
			filter_rule_set_name(rule, name);
			g_free(name);
		}
		g_free(mlist);
	}
}

FilterRule *
vfolder_rule_from_message (VfolderContext *context, CamelMimeMessage *msg, int flags, const char *source)
{
	VfolderRule *rule;
	
	rule = vfolder_rule_new ();
	vfolder_rule_add_source (rule, source);
	rule_from_message ((FilterRule *)rule, (RuleContext *)context, msg, flags);
	
	return (FilterRule *)rule;
}

FilterRule *
filter_rule_from_message (FilterContext *context, CamelMimeMessage *msg, int flags)
{
	FilterFilter *rule;
	FilterPart *part;
	
	rule = filter_filter_new ();
	rule_from_message ((FilterRule *)rule, (RuleContext *)context, msg, flags);
	
	part = filter_context_next_action (context, NULL);
	filter_filter_add_action (rule, filter_part_clone (part));
	
	return (FilterRule *)rule;
}

void
filter_gui_add_from_message (CamelMimeMessage *msg, const char *source, int flags)
{
	FilterContext *fc;
	char *user, *system;
	FilterRule *rule;

	g_return_if_fail (msg != NULL);
	
	fc = filter_context_new ();
	user = g_strdup_printf ("%s/filters.xml", evolution_dir);
	system = EVOLUTION_DATADIR "/evolution/filtertypes.xml";
	rule_context_load ((RuleContext *)fc, system, user);
	rule = filter_rule_from_message (fc, msg, flags);
	
	filter_rule_set_source (rule, source);
	
	rule_context_add_rule_gui ((RuleContext *)fc, rule, _("Add Filter Rule"), user);
	g_free (user);
	gtk_object_unref (GTK_OBJECT (fc));
}

void
mail_filter_rename_uri(CamelStore *store, const char *olduri, const char *newuri)
{
	GCompareFunc uri_cmp = CAMEL_STORE_CLASS(CAMEL_OBJECT_GET_CLASS(store))->compare_folder_name;
	FilterContext *fc;
	char *user, *system;
	GList *changed;

	fc = filter_context_new ();
	user = g_strdup_printf ("%s/filters.xml", evolution_dir);
	system = EVOLUTION_DATADIR "/evolution/filtertypes.xml";
	rule_context_load ((RuleContext *)fc, system, user);

	changed = rule_context_rename_uri((RuleContext *)fc, olduri, newuri, uri_cmp);
	if (changed) {
		printf("Folder rename '%s' -> '%s' changed filters, resaving\n", olduri, newuri);
		if (rule_context_save((RuleContext *)fc, user) == -1)
			g_warning("Could not write out changed filter rules\n");
		rule_context_free_uri_list((RuleContext *)fc, changed);
	}

	g_free(user);
	gtk_object_unref (GTK_OBJECT (fc));
}

void
mail_filter_delete_uri(CamelStore *store, const char *uri)
{
	GCompareFunc uri_cmp = CAMEL_STORE_CLASS(CAMEL_OBJECT_GET_CLASS(store))->compare_folder_name;
	FilterContext *fc;
	char *user, *system;
	GList *deleted;

	fc = filter_context_new ();
	user = g_strdup_printf ("%s/filters.xml", evolution_dir);
	system = EVOLUTION_DATADIR "/evolution/filtertypes.xml";
	rule_context_load ((RuleContext *)fc, system, user);

	deleted = rule_context_delete_uri((RuleContext *)fc, uri, uri_cmp);
	if (deleted) {
		GnomeDialog *gd;
		GString *s;
		GList *l;

		s = g_string_new(_("The following filter rule(s):\n"));
		l = deleted;
		while (l) {
			g_string_sprintfa(s, "    %s\n", (char *)l->data);
			l = l->next;
		}
		g_string_sprintfa(s, _("Used the removed folder:\n    '%s'\n"
				       "And have been updated."), uri);
		gd = (GnomeDialog *)gnome_warning_dialog(s->str);
		g_string_free(s, TRUE);
		gnome_dialog_set_close(gd, TRUE);
		gtk_widget_show((GtkWidget *)gd);

		printf("Folder deleterename '%s' changed filters, resaving\n", uri);
		if (rule_context_save((RuleContext *)fc, user) == -1)
			g_warning("Could not write out changed filter rules\n");
		rule_context_free_uri_list((RuleContext *)fc, deleted);
	}

	g_free(user);
	gtk_object_unref (GTK_OBJECT (fc));
}
