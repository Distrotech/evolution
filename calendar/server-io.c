#include "calserv.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

void cs_connection_accept(gpointer data, GIOCondition cond,
			  CSServer *server);
static void cs_connection_process(gpointer data, GIOCondition cond,
				  CSConnection *cnx);
static void cs_connection_greet(CSConnection *cnx);

CSServer *
cs_server_new(void)
{
  CSServer *rv;
  struct sockaddr_in addr;

  rv = g_new0(CSServer, 1);

  rv->mainloop = g_main_new();
  rv->servfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if(rv->servfd < 0) goto errout;

  {
    int n = 1;
    setsockopt(rv->servfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));
  }

  addr.sin_family = AF_INET;
  addr.sin_port = htons(7668);
  addr.sin_addr.s_addr = INADDR_ANY;
  if(bind(rv->servfd, &addr, sizeof(addr))) goto errout;
  if(listen(rv->servfd, 1)) goto errout;

  /* FTSO this GIOChannel crap */
  rv->gioc = g_io_channel_unix_new(rv->servfd);
  g_io_add_watch(rv->gioc, G_IO_IN, (GIOFunc)&cs_connection_accept, rv);
  g_io_channel_ref(rv->gioc);

  return rv;

 errout:
  if(rv->gioc)
    g_io_channel_unref(rv->gioc);
  close(rv->servfd);
  g_free(rv);
  return NULL;
}

void
cs_server_run(CSServer *server)
{
  g_return_if_fail(server);

  g_main_run(server->mainloop);
}

void
cs_server_destroy(CSServer *server)
{
  g_return_if_fail(server);
  
  g_main_quit(server->mainloop);
  g_main_destroy(server->mainloop);

  close(server->servfd);
  g_io_channel_unref(server->gioc);

  g_list_foreach(server->connections, (GFunc)cs_connection_destroy, NULL);

  g_free(server);
}

static void
cs_connection_process(gpointer data, GIOCondition cond,
		      CSConnection *cnx)
{
  char readbuf[257], *ctmp;
  int rsize, itmp;
  gboolean found_something, is_eol;

  if(cond & (G_IO_HUP|G_IO_NVAL|G_IO_ERR)) {
    cs_connection_destroy(cnx);
    return;
  }

  g_return_if_fail(cond & G_IO_IN);

  /* read the data */
  rsize = read(cnx->fd, readbuf, sizeof(readbuf) - 1);
  if(!rsize) {
    cs_connection_destroy(cnx);
    return;
  }
  readbuf[rsize] = '\0';
  g_string_append(cnx->rdbuf, readbuf);

  do {
    found_something = FALSE;
    switch(cnx->rs) {
    case RS_ID:
      ctmp = strchr(cnx->rdbuf->str, ' ');
      if(ctmp) {
	itmp = ctmp - cnx->rdbuf->str;
	cnx->rs = RS_NAME;
	cnx->curcmd.id = g_strndup(cnx->rdbuf->str, itmp);
	g_string_erase(cnx->rdbuf, 0, itmp + 1);
	found_something = TRUE;
      }
      break;
    case RS_NAME:
      is_eol = FALSE;
      ctmp = strchr(cnx->rdbuf->str, ' ');

      if(!ctmp) {
	ctmp = strchr(cnx->rdbuf->str, '\r');
	if(!ctmp) break;

	is_eol = TRUE;
      }
      found_something = TRUE;
      itmp = ctmp - cnx->rdbuf->str;

      if(is_eol)
	cnx->rs = RS_DONE;
      else {
	cnx->rs = RS_ARG;
	cnx->curarg = cnx->curcmd.args = NULL;
      }

      cnx->curcmd.name = g_strndup(cnx->rdbuf->str, itmp);
      g_string_erase(cnx->rdbuf, 0, itmp + 1);
      break;
    case RS_ARG:
      if(cnx->in_literal) {
	cnx->literal_left -= rsize;

	if(cnx->literal_left <= 0) {
	  cnx->curarg->data = g_strndup(cnx->rdbuf->str, cnx->in_literal);
	  cnx->in_literal = cnx->literal_left = 0;
	  g_string_erase(cnx->rdbuf, 0, cnx->in_literal + 2 /* CRLF */);
	  found_something = TRUE;
	}

	break;
      }

      switch(*cnx->rdbuf->str) {
      case '(': /* open list */
	cnx->curarg = cs_cmdarg_new(NULL, cnx->curarg);
	if(!cnx->curcmd.args) cnx->curcmd.args = cnx->curarg;
	cnx->curarg->type = ITEM_SUBLIST;
	g_string_erase(cnx->rdbuf, 0, 1);
	found_something = TRUE;
	break;
      case ')': /* close list */
	if(!cnx->curarg) {
	  g_warning("Too many close parens. zonk.");
	  cs_connection_destroy(cnx);
	  return;
	}
	cnx->curarg = cnx->curarg->up;
	break;
      case '{': /* literal */
	ctmp = strchr(cnx->rdbuf->str + 1, '}');
	if(!ctmp) break;
	if(*(ctmp + 1) != '\r' /* lameness */
	   || sscanf(cnx->rdbuf->str + 1, "%d", &cnx->in_literal) < 1) {
	  g_warning("bad literal. zonk.");
	  cs_connection_destroy(cnx);
	  return;
	}
	found_something = TRUE;
	cnx->literal_left = cnx->in_literal + 2;
	g_string_erase(cnx->rdbuf, 0, ctmp - cnx->rdbuf->str + 2 /* eliminate CR */);
	break;
      case '"':
	ctmp = strchr(cnx->rdbuf->str + 1, '"');
	if(!ctmp) break;
	found_something = TRUE;
	cnx->curarg = cs_cmdarg_new(cnx->curarg, NULL);
	if(!cnx->curcmd.args) cnx->curcmd.args = cnx->curarg;
	cnx->curarg->type = ITEM_STRING;
	cnx->curarg->data = g_strndup(cnx->rdbuf->str + 1, ctmp - cnx->rdbuf->str - 1);
	g_string_erase(cnx->rdbuf, 0, ctmp - cnx->rdbuf->str);
	break;
      case ' ':
	found_something = cnx->rdbuf->len;
	cnx->in_literal = cnx->literal_left = cnx->in_quoted = 0;
	g_string_erase(cnx->rdbuf, 0, 1);
	break;
      case '\r':
	if(cnx->rdbuf->len > 2) {
	  cnx->rs = RS_DONE;
	  g_string_erase(cnx->rdbuf, 0, 2);
	}
	break;
      default: /* warning, massive code duplication */
	/* it is a string */
	ctmp = strchr(cnx->rdbuf->str, ' ');
	if(ctmp) {
	  found_something = TRUE;
	  cnx->curarg = cs_cmdarg_new(cnx->curarg, NULL);
	  if(!cnx->curcmd.args) cnx->curcmd.args = cnx->curarg;
	  cnx->curarg->type = ITEM_STRING;
	  cnx->curarg->data = g_strndup(cnx->rdbuf->str, ctmp - cnx->rdbuf->str);
	  g_string_erase(cnx->rdbuf, 0, ctmp - cnx->rdbuf->str);
	  break;
	}
	ctmp = strchr(cnx->rdbuf->str, ')');
	if(ctmp) {
	  found_something = TRUE;
	  cnx->curarg = cs_cmdarg_new(cnx->curarg, NULL);
	  if(!cnx->curcmd.args) cnx->curcmd.args = cnx->curarg;
	  cnx->curarg->type = ITEM_STRING;
	  cnx->curarg->data = g_strndup(cnx->rdbuf->str, ctmp - cnx->rdbuf->str);
	  cnx->curarg = cnx->curarg->up;
	  g_string_erase(cnx->rdbuf, 0, ctmp - cnx->rdbuf->str);
	  break;
	}
	ctmp = strchr(cnx->rdbuf->str, '\r');
	if(ctmp) {
	  found_something = TRUE;
	  cnx->rs = RS_DONE;
	  cnx->curarg = cs_cmdarg_new(cnx->curarg, NULL);
	  if(!cnx->curcmd.args) cnx->curcmd.args = cnx->curarg;
	  cnx->curarg->type = ITEM_STRING;
	  cnx->curarg->data = g_strndup(cnx->rdbuf->str, ctmp - cnx->rdbuf->str);
	  g_string_erase(cnx->rdbuf, 0, ctmp - cnx->rdbuf->str);
	  break;
	}
	break;
      }
      break;
    default:
    }

    if(cnx->rs == RS_DONE) {
      cs_connection_process_command(cnx);
      cs_cmdarg_destroy(cnx->curcmd.args); cnx->curcmd.args = NULL;
      g_free(cnx->curcmd.id);
      g_free(cnx->curcmd.name);
      cnx->rs = RS_ID; /* Next? */
    }
  } while(found_something);
}

static void
cs_connection_greet(CSConnection *cnx)
{
#define CS_greeting "* OK "CS_capabilities" It is actually quite an aweful day today.\n"

  write(cnx->fd, CS_greeting, sizeof(CS_greeting)-1);
}

void cs_connection_accept(gpointer data, GIOCondition cond,
			  CSServer *server)
{
  CSConnection *cnx;
  int fd, itmp;
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  itmp = sizeof(addr);
  fd = accept(server->servfd, &addr, &itmp);
  if(fd < 0) return;

  cnx = g_new0(CSConnection, 1);
  cnx->rdbuf = g_string_new(NULL);
  cnx->serv = server;
  cnx->fd = fd;
  fcntl(cnx->fd, F_GETFL, &itmp);
  itmp |= O_NONBLOCK;
  fcntl(cnx->fd, F_SETFL, &itmp);  

  cnx->fh = fdopen(fd, "a+");
  setvbuf(cnx->fh, cnx->wrbuf, _IOLBF, sizeof(cnx->wrbuf));
  g_assert(cnx->fh);

  cnx->gioc = g_io_channel_unix_new(fd);
  g_io_channel_ref(cnx->gioc);
  g_io_add_watch(cnx->gioc, G_IO_IN|G_IO_HUP|G_IO_NVAL,
		 (GIOFunc)&cs_connection_process, cnx);

  server->connections = g_list_prepend(server->connections, cnx);
  cs_connection_greet(cnx);
}

void
cs_connection_destroy(CSConnection *cnx)
{
  CSServer *server;

  server = cnx->serv;
  server->connections = g_list_remove(server->connections, cnx);
  fclose(cnx->fh); /* also closes fd */
  g_io_channel_unref(cnx->gioc);
  g_string_free(cnx->rdbuf, TRUE);
  g_free(cnx->authid);
  if(cnx->active_cal)
    backend_close_calendar(cnx->active_cal);
  g_free(cnx);
}     
