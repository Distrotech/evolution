/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Not Zed <notzed@lostzed.mmc.com.au>
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
 */

#ifndef _FILTER_RULE_H
#define _FILTER_RULE_H

#include <gtk/gtkobject.h>
#include "filter-part.h"

#define FILTER_RULE(obj)	GTK_CHECK_CAST (obj, filter_rule_get_type (), FilterRule)
#define FILTER_RULE_CLASS(klass)	GTK_CHECK_CLASS_CAST (klass, filter_rule_get_type (), FilterRuleClass)
#define IS_FILTER_RULE(obj)      GTK_CHECK_TYPE (obj, filter_rule_get_type ())

typedef struct _FilterRule	FilterRule;
typedef struct _FilterRuleClass	FilterRuleClass;

struct _RuleContext;

enum _filter_grouping_t {
	FILTER_GROUP_ALL,	/* all rules must match */
	FILTER_GROUP_ANY	/* any rule must match */
};


#define FILTER_SOURCE_INCOMING "incoming" /* performed on incoming email */
#define FILTER_SOURCE_DEMAND   "demand"   /* performed on the selected folder
	 				   * when the user asks for it */
#define	FILTER_SOURCE_OUTGOING  "outgoing"/* performed on outgoing mail */

struct _FilterRule {
	GtkObject parent;
	struct _FilterRulePrivate *priv;
	
	char *name;
	char *source;
	
	enum _filter_grouping_t grouping;
	GList *parts;
};

struct _FilterRuleClass {
	GtkObjectClass parent_class;
	
	/* virtual methods */
	int (*validate)(FilterRule *);

	xmlNodePtr (*xml_encode)(FilterRule *);
	int (*xml_decode)(FilterRule *, xmlNodePtr, struct _RuleContext *);
	
	void (*build_code)(FilterRule *, GString *out);

	void (*copy)(FilterRule *dest, FilterRule *src);
	
	GtkWidget *(*get_widget)(FilterRule *fr, struct _RuleContext *f);
	
	/* signals */
	void (*changed)(FilterRule *fr);
};

guint		filter_rule_get_type	(void);
FilterRule	*filter_rule_new	(void);

FilterRule 	*filter_rule_clone	(FilterRule *base);

/* methods */
void		filter_rule_set_name	(FilterRule *fr, const char *name);
void		filter_rule_set_source	(FilterRule *fr, const char *source);

int		filter_rule_validate	(FilterRule *fr);

xmlNodePtr	filter_rule_xml_encode	(FilterRule *fr);
int		filter_rule_xml_decode	(FilterRule *fr, xmlNodePtr node, struct _RuleContext *f);

void            filter_rule_copy        (FilterRule *dest, FilterRule *src);

void		filter_rule_add_part	(FilterRule *fr, FilterPart *fp);
void		filter_rule_remove_part	(FilterRule *fr, FilterPart *fp);
void		filter_rule_replace_part(FilterRule *fr, FilterPart *fp, FilterPart *new);

GtkWidget	*filter_rule_get_widget	(FilterRule *fr, struct _RuleContext *f);

void		filter_rule_build_code	(FilterRule *fr, GString *out);
/*
void		filter_rule_build_action(FilterRule *fr, GString *out);
*/

void		filter_rule_emit_changed	(FilterRule *fr);

/* static functions */
FilterRule	*filter_rule_next_list		(GList *l, FilterRule *last, const char *source);
FilterRule	*filter_rule_find_list		(GList *l, const char *name, const char *source);


#endif /* ! _FILTER_RULE_H */

