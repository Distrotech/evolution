/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2002 Ximian, Inc. (www.ximian.com)
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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include "e-trie.h"
#include "e-memory.h"

#define d(x)

struct _trie_state {
	struct _trie_state *next;
	struct _trie_state *fail;
	struct _trie_match *match;
	unsigned int final;
	int id;
};

struct _trie_match {
	struct _trie_match *next;
	struct _trie_state *state;
	gunichar c;
};

struct _ETrie {
	struct _trie_state root;
	GPtrArray *fail_states;
	gboolean icase;
	
	EMemChunk *match_chunks;
	EMemChunk *state_chunks;
};


static __inline__ gunichar
trie_utf8_getc (const unsigned char **in, size_t inlen)
{
	register const unsigned char *inptr = *in;
	const unsigned char *inend = inptr + inlen;
	register unsigned char c, r;
	register gunichar u, m;
	
	if (inlen == 0)
		return 0;
	
again:
	r = *inptr++;
loop:
	if (r < 0x80) {
		*in = inptr;
		u = r;
	} else if (r < 0xfe) { /* valid start char? */
		u = r;
		m = 0x7f80;	/* used to mask out the length bits */
		do {
			if (inptr >= inend)
				return 0;
			
			c = *inptr++;
			if ((c & 0xc0) != 0x80) {
				r = c;
				goto loop;
			}
			
			u = (u << 6) | (c & 0x3f);
			r <<= 1;
			m <<= 5;
		} while (r & 0x40);
		
		*in = inptr;
		
		u &= ~m;
	} else {
		goto again;
	}
	
	return u;
}


ETrie *
e_trie_new (gboolean icase)
{
	ETrie *trie;
	
	trie = g_new (ETrie, 1);
	trie->root.next = NULL;
	trie->root.fail = NULL;
	trie->root.match = NULL;
	trie->root.final = 0;
	
	trie->fail_states = g_ptr_array_sized_new (8);
	trie->icase = icase;
	
	trie->match_chunks = e_memchunk_new (8, sizeof (struct _trie_match));
	trie->state_chunks = e_memchunk_new (8, sizeof (struct _trie_state));
	
	return trie;
}

void
e_trie_free (ETrie *trie)
{
	e_memchunk_destroy (trie->match_chunks);
	e_memchunk_destroy (trie->state_chunks);
	g_free (trie);
}



static struct _trie_match *
g (struct _trie_state *s, gunichar c)
{
	struct _trie_match *m = s->match;
	
	while (m && m->c != c)
		m = m->next;
	
	return m;
}

static struct _trie_state *
trie_insert (ETrie *trie, int depth, struct _trie_state *q, gunichar c)
{
	struct _trie_match *m;
	
	m = e_memchunk_alloc (trie->match_chunks);
	m->next = q->match;
	m->c = c;
	
	q->match = m;
	q = m->state = e_memchunk_alloc (trie->state_chunks);
	q->match = NULL;
	q->fail = &trie->root;
	q->final = 0;
	q->id = -1;
	
	if (trie->fail_states->len < depth + 1) {
		unsigned int size = trie->fail_states->len;
		
		size = MAX (size + 64, depth + 1);
		g_ptr_array_set_size (trie->fail_states, size);
	}
	
	q->next = trie->fail_states->pdata[depth];
	trie->fail_states->pdata[depth] = q;
	
	return q;
}


#if 1
static void 
dump_trie (struct _trie_state *s, int depth)
{
	char *p = g_alloca ((depth * 2) + 1);
	struct _trie_match *m;
	
	memset (p, ' ', depth * 2);
	p[depth * 2] = '\0';
	
	fprintf (stderr, "%s[state] %p: final=%d; pattern-id=%d; fail=%p\n",
		 p, s, s->final, s->id, s->fail);
	m = s->match;
	while (m) {
		fprintf (stderr, " %s'%c' -> %p\n", p, m->c, m->state);
		if (m->state)
			dump_trie (m->state, depth + 1);
		
		m = m->next;
	}
}
#endif


/*
 * final = empty set
 * FOR p = 1 TO #pat
 *   q = root
 *   FOR j = 1 TO m[p]
 *     IF g(q, pat[p][j]) == null
 *       insert(q, pat[p][j])
 *     ENDIF
 *     q = g(q, pat[p][j])
 *   ENDFOR
 *   final = union(final, q)
 * ENDFOR
*/

void
e_trie_add (ETrie *trie, const char *pattern, int pattern_id)
{
	const unsigned char *inptr = (const unsigned char *) pattern;
	struct _trie_state *q, *q1, *r;
	struct _trie_match *m, *n;
	int i, depth = 0;
	gunichar c;
	
	/* Step 1: add the pattern to the trie */
	
	q = &trie->root;
	
	while ((c = trie_utf8_getc (&inptr, -1))) {
		if (trie->icase)
			c = g_unichar_tolower (c);
		
		m = g (q, c);
		if (m == NULL) {
			q = trie_insert (trie, depth, q, c);
		} else {
			q = m->state;
		}
		
		depth++;
	}
	
	q->final = depth;
	q->id = pattern_id;
	
	/* Step 2: compute failure graph */
	
	for (i = 0; i < trie->fail_states->len; i++) {
		q = trie->fail_states->pdata[i];
		while (q) {
			m = q->match;
			while (m) {
				c = m->c;
				q1 = m->state;
				r = q->fail;
				while (r && (n = g (r, c)) == NULL)
					r = r->fail;
				
				if (r != NULL) {
					q1->fail = n->state;
					if (q1->fail->final > q1->final)
						q1->final = q1->fail->final;
				} else {
					if ((n = g (&trie->root, c)))
						q1->fail = n->state;
					else
						q1->fail = &trie->root;
				}
				
				m = m->next;
			}
			
			q = q->next;
		}
	}
	
	d(fprintf (stderr, "\nafter adding pattern '%s' to trie %p:\n", pattern, trie));
	d(dump_trie (&trie->root, 0));
}

/*
 * Aho-Corasick
 *
 * q = root
 * FOR i = 1 TO n
 *   WHILE q != fail AND g(q, text[i]) == fail
 *     q = h(q)
 *   ENDWHILE
 *   IF q == fail
 *     q = root
 *   ELSE
 *     q = g(q, text[i])
 *   ENDIF
 *   IF isElement(q, final)
 *     RETURN TRUE
 *   ENDIF
 * ENDFOR
 * RETURN FALSE
 */

const char *
e_trie_search (ETrie *trie, const char *buffer, size_t buflen, int *matched_id)
{
	const unsigned char *inptr, *inend, *prev, *pat;
	register size_t inlen = buflen;
	struct _trie_state *q;
	struct _trie_match *m;
	gunichar c;
	
	inptr = (const unsigned char *) buffer;
	inend = inptr + buflen;
	
	q = &trie->root;
	pat = prev = inptr;
	while ((c = trie_utf8_getc (&inptr, inlen))) {
		inlen = (inend - inptr);
		
		if (trie->icase)
			c = g_unichar_tolower (c);
		
		while (q != NULL && (m = g (q, c)) == NULL)
			q = q->fail;
		
		if (q == &trie->root)
			pat = prev;
		
		if (q == NULL) {
			q = &trie->root;
			pat = inptr;
		} else if (m != NULL) {
			q = m->state;
			
			if (q->final) {
				if (matched_id)
					*matched_id = q->id;
				
				return (const char *) pat;
			}
		}
		
		prev = inptr;
	}
	
	return NULL;
}
