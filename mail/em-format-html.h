
/*
  Concrete class for formatting mails to html
*/

#ifndef _EM_FORMAT_HTML_H
#define _EM_FORMAT_HTML_H

#include "em-format.h"

typedef struct _EMFormatHTML EMFormatHTML;
typedef struct _EMFormatHTMLClass EMFormatHTMLClass;

#if 0
struct _EMFormatHTMLHandler {
	EFrormatHandler base;
};
#endif

struct _GtkHTMLEmbedded;
struct _CamelMimePart;
struct _CamelStream;

/* its ugly but ... oh well */
typedef struct _EMFormatHTMLPObject EMFormatHTMLPObject;

typedef gboolean (*EMFormatHTMLPObjectFunc)(EMFormatHTML *md, struct _GtkHTMLEmbedded *eb, EMFormatHTMLPObject *pobject);

struct _EMFormatHTMLPObject {
	struct _EMFormatHTMLPObject *next, *prev;

	char *classid;

	EMFormatHTMLPObjectFunc func;
	struct _CamelMimePart *part;
};

#define EM_FORMAT_HTML_HEADER_NOCOLUMNS (EM_FORMAT_HEADER_LAST)
#define EM_FORMAT_HTML_HEADER_LAST (EM_FORMAT_HEADER_LAST<<8)

struct _EMFormatHTML {
	EMFormat format;

	struct _GtkHTML *html;

	EDList pending_object_list;

	GSList *headers;

	guint32 text_html_flags; /* default flags for text to html conversion */
	guint32 header_colour;	/* header box colour */
	guint32 text_colour;
	guint32 citation_colour;
	guint32 xmailer_mask;	/* this should probably die? */
	unsigned int load_http:1;
	unsigned int mark_citations:1;
};

struct _EMFormatHTMLClass {
	EMFormatClass format_class;
};

GType em_format_html_get_type(void);
EMFormatHTML *em_format_html_new(void);

/* retrieves a pseudo-part icon wrapper for a file */
struct _CamelMimePart *em_format_html_file_part(EMFormatHTML *efh, const char *mime_type, const char *path, const char *name);

/* for implementers */
const char *em_format_html_add_pobject(EMFormatHTML *efh, const char *classid, EMFormatHTMLPObjectFunc func, struct _CamelMimePart *part);
EMFormatHTMLPObject * em_format_html_find_pobject(EMFormatHTML *emf, const char *classid);
EMFormatHTMLPObject *em_format_html_find_pobject_func(EMFormatHTML *emf, struct _CamelMimePart *part, EMFormatHTMLPObjectFunc func);
void em_format_html_remove_pobject(EMFormatHTML *emf, EMFormatHTMLPObject *pobject);

/* outputs a signature test */
void em_format_html_multipart_signed_sign(EMFormat *emf, struct _CamelStream *stream, struct _CamelMimePart *part);

#endif /* ! EM_FORMAT_HTML_H */
