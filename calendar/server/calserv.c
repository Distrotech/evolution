#include "calserv.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

static GSourceFuncs serv_fd_funcs = {NULL, NULL, &cs_connection_accept,
				     &cs_connection_destroy};
CSServer *
cs_server_new(void)
{
  CSServer *rv;
  struct sockaddr_in addr;

  rv = g_new0(CSServer, 1);

  rv->mainloop = g_main_new();
  rv->servfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if(rv->servfd < 0) goto errout;

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
  GList *ltmp, *nltmp;
  g_return_if_fail(server);
  
  g_main_quit(server->mainloop);
  g_main_destroy(server->mainloop);

  close(server->servfd);
  g_io_channel_unref(server->gioc);

  for(ltmp = server->connections; ltmp; ltmp = nltmp) {
    nltmp = ltmp->next;
    cs_connection_destroy(ltmp->data);
  }

  g_free(server);
}

static void
cs_connection_process(gpointer data, GIOCondition cond,
		      CSConnection *cnx)
{
  if(cond & G_IO_HUP) {
    cs_connection_destroy(cnx);
    return;
  }
}

static void
cs_connection_greet(CSConnection *cnx)
{
#define CS_greeting "* OK ICAP It is actually quite an aweful day today.\n"

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
  cnx->serv = server;
  cnx->fd = fd;
  cnx->gioc = g_io_channel_unix_new(fd);
  g_io_channel_ref(cnx->gioc);
  g_io_add_watch(cnx->gioc, G_IO_IN|G_IO_HUP|G_IO_NVAL,
		 &cs_connection_process, cnx);

  server->connections = g_list_prepend(server->connections, cnx);
  cs_connection_greet(cnx);
}

void
cs_connection_destroy(CSConnection *cnx)
{
  CSServer *server;

  server = cnx->serv;
  server->connections = g_list_remove(server->connections, cnx);
  close(cnx->fd);
  g_io_channel_unref(cnx->gioc);
  g_free(cnx);
}     
