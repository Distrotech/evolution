/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Not Zed <notzed@lostzed.mmc.com.au>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _SCORE_EDITOR_H
#define _SCORE_EDITOR_H

#include "rule-editor.h"

#define SCORE_EDITOR(obj)	GTK_CHECK_CAST (obj, score_editor_get_type (), ScoreEditor)
#define SCORE_EDITOR_CLASS(klass)	GTK_CHECK_CLASS_CAST (klass, score_editor_get_type (), ScoreEditorClass)
#define IS_SCORE_EDITOR(obj)      GTK_CHECK_TYPE (obj, score_editor_get_type ())

typedef struct _ScoreEditor	ScoreEditor;
typedef struct _ScoreEditorClass	ScoreEditorClass;

struct _ScoreEditor {
	RuleEditor parent;

	struct _ScoreEditorPrivate *priv;
};

struct _ScoreEditorClass {
	RuleEditorClass parent_class;

	/* virtual methods */

	/* signals */
};

struct _ScoreContext;

guint		score_editor_get_type	(void);
ScoreEditor	*score_editor_new	(struct _ScoreContext *f);

#endif /* ! _SCORE_EDITOR_H */

