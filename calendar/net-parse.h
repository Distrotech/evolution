/* for bandying around info on a command */
typedef struct _CSCmdArg CSCmdArg;

struct _CSCmdArg {
  enum {
	  ITEM_UNKNOWN = 0,
	  ITEM_STRING,
	  ITEM_SUBLIST
  } type;
  gpointer data;
  CSCmdArg *next, *up;
};

CSCmdArg *cs_cmdarg_new     (CSCmdArg *prev, CSCmdArg *parent);
gint      cs_cmdarg_nargs   (CSCmdArg *arglist);
void      cs_cmdarg_destroy (CSCmdArg *arg);

typedef struct {
  char *id;
  char *name;
  CSCmdArg *args;
} CSCmdInfo;

typedef struct {
	GString *rdbuf;

	/* read state */
	enum {
		RS_ID = 0,
		RS_NAME,
		RS_ARG,
		RS_DONE
	} rs;

	gint in_literal;
	gint literal_left;
	gint in_quoted; /* 0 if not currently reading */
	CSCmdArg *curarg;
	CSCmdInfo curcmd;
} StreamParse;

int try_to_parse (StreamParse *parse, int rsize, gboolean *error, gboolean *cont);
