

#include <glib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "e-msgport.h"

#include <pthread.h>

void e_dlist_init(EDList *v)
{
        v->head = (EDListNode *)&v->tail;
        v->tail = 0;
        v->tailpred = (EDListNode *)&v->head;
}

EDListNode *e_dlist_addhead(EDList *l, EDListNode *n)
{
        n->next = l->head;
        n->prev = (EDListNode *)&l->head;
        l->head->prev = n;
        l->head = n;
        return n;
}

EDListNode *e_dlist_addtail(EDList *l, EDListNode *n)
{
        n->next = (EDListNode *)&l->tail;
        n->prev = l->tailpred;
        l->tailpred->next = n;
        l->tailpred = n;
        return n;
}

EDListNode *e_dlist_remove(EDListNode *n)
{
        n->next->prev = n->prev;
        n->prev->next = n->next;
        return n;
}

EDListNode *e_dlist_remhead(EDList *l)
{
	EDListNode *n, *nn;

	n = l->head;
	nn = n->next;
	if (nn) {
		nn->prev = n->prev;
		l->head = nn;
		return n;
	}
	return NULL;
}

EDListNode *e_dlist_remtail(EDList *l)
{
	EDListNode *n, *np;

	n = l->tailpred;
	np = n->prev;
	if (np) {
		np->next = n->next;
		l->tailpred = np;
		return n;
	}
	return NULL;
}

int e_dlist_empty(EDList *l)
{
	return (l->head == (EDListNode *)&l->tail);
}


struct _EMsgPort {
	EDList queue;
	int condwait;		/* how many waiting in condwait */
	union {
		int pipe[2];
		struct {
			int read;
			int write;
		} fd;
	} pipe;
	/* @#@$#$ glib stuff */
	GCond *cond;
	GMutex *lock;
};

#define m(x) 

EMsgPort *e_msgport_new(void)
{
	EMsgPort *mp;

	mp = g_malloc(sizeof(*mp));
	e_dlist_init(&mp->queue);
	mp->lock = g_mutex_new();
	mp->cond = g_cond_new();
	mp->pipe.fd.read = -1;
	mp->pipe.fd.write = -1;
	mp->condwait = 0;

	return mp;
}

void e_msgport_destroy(EMsgPort *mp)
{
	g_mutex_free(mp->lock);
	g_cond_free(mp->cond);
	if (mp->pipe.fd.read != -1) {
		close(mp->pipe.fd.read);
		close(mp->pipe.fd.write);
	}
	g_free(mp);
}

/* get a fd that can be used to wait on the port asynchronously */
int e_msgport_fd(EMsgPort *mp)
{
	int fd;

	g_mutex_lock(mp->lock);
	fd = mp->pipe.fd.read;
	if (fd == -1) {
		pipe(mp->pipe.pipe);
		fd = mp->pipe.fd.read;
	}
	g_mutex_unlock(mp->lock);

	return fd;
}

void e_msgport_put(EMsgPort *mp, EMsg *msg)
{
	m(printf("put:\n"));
	g_mutex_lock(mp->lock);
	e_dlist_addtail(&mp->queue, &msg->ln);
	if (mp->condwait > 0) {
		m(printf("put: condwait > 0, waking up\n"));
		g_cond_signal(mp->cond);
	}
	if (mp->pipe.fd.write != -1) {
		m(printf("put: have pipe, writing notification to it\n"));
		write(mp->pipe.fd.write, "", 1);
	}
	g_mutex_unlock(mp->lock);
	m(printf("put: done\n"));
}

EMsg *e_msgport_wait(EMsgPort *mp)
{
	EMsg *msg;

	m(printf("wait:\n"));
	g_mutex_lock(mp->lock);
	while (e_dlist_empty(&mp->queue)) {
		if (mp->pipe.fd.read == -1) {
			m(printf("wait: waiting on condition\n"));
			mp->condwait++;
			g_cond_wait(mp->cond, mp->lock);
			m(printf("wait: got condition\n"));
			mp->condwait--;
		} else {
			fd_set rfds;

			m(printf("wait: waitng on pipe\n"));
			FD_ZERO(&rfds);
			FD_SET(mp->pipe.fd.read, &rfds);
			g_mutex_unlock(mp->lock);
			select(mp->pipe.fd.read+1, &rfds, NULL, NULL, NULL);
			g_mutex_lock(mp->lock);
			m(printf("wait: got pipe\n"));
		}
	}
	msg = (EMsg *)mp->queue.head;
	m(printf("wait: message = %p\n", msg));
	g_mutex_unlock(mp->lock);
	m(printf("wait: done\n"));
	return msg;
}

EMsg *e_msgport_get(EMsgPort *mp)
{
	EMsg *msg;
	char dummy[1];

	g_mutex_lock(mp->lock);
	msg = (EMsg *)e_dlist_remhead(&mp->queue);
	if (msg && mp->pipe.fd.read != -1)
		read(mp->pipe.fd.read, dummy, 1);
	m(printf("get: message = %p\n", msg));
	g_mutex_unlock(mp->lock);

	return msg;
}

void e_msgport_reply(EMsg *msg)
{
	if (msg->reply_port) {
		e_msgport_put(msg->reply_port, msg);
	}
	/* else lost? */
}

struct _EMutex {
	int type;
	pthread_t owner;
	short waiters;
	short depth;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

/* sigh, this is just painful to have to need, but recursive
   read/write, etc mutexes just aren't very common in thread
   implementations */
/* TODO: Just make it use recursive mutexes if they are available */
EMutex *e_mutex_new(e_mutex_t type)
{
	struct _EMutex *m;

	m = g_malloc(sizeof(*m));
	m->type = type;
	m->waiters = 0;
	m->depth = 0;
	m->owner = ~0;

	switch (type) {
	case E_MUTEX_SIMPLE:
		pthread_mutex_init(&m->mutex, 0);
		break;
	case E_MUTEX_REC:
		pthread_mutex_init(&m->mutex, 0);
		pthread_cond_init(&m->cond, 0);
		break;
		/* read / write ?  flags for same? */
	}

	return m;
}

int e_mutex_destroy(EMutex *m)
{
	int ret = 0;

	switch (m->type) {
	case E_MUTEX_SIMPLE:
		ret = pthread_mutex_destroy(&m->mutex);
		if (ret == -1)
			g_warning("EMutex destroy failed: %s", strerror(errno));
		g_free(m);
		break;
	case E_MUTEX_REC:
		ret = pthread_mutex_destroy(&m->mutex);
		if (ret == -1)
			g_warning("EMutex destroy failed: %s", strerror(errno));
		ret = pthread_cond_destroy(&m->cond);
		if (ret == -1)
			g_warning("EMutex destroy failed: %s", strerror(errno));
		g_free(m);

	}
	return ret;
}

int e_mutex_lock(EMutex *m)
{
	pthread_t id;

	switch (m->type) {
	case E_MUTEX_SIMPLE:
		return pthread_mutex_lock(&m->mutex);
	case E_MUTEX_REC:
		id = pthread_self();
		if (pthread_mutex_lock(&m->mutex) == -1)
			return -1;
		while (1) {
			if (m->owner == ~0) {
				m->owner = id;
				m->depth = 1;
				break;
			} else if (id == m->owner) {
				m->depth++;
				break;
			} else {
				m->waiters++;
				if (pthread_cond_wait(&m->cond, &m->mutex) == -1)
					return -1;
				m->waiters--;
			}
		}
		return pthread_mutex_unlock(&m->mutex);
	}

	errno = EINVAL;
	return -1;
}

int e_mutex_unlock(EMutex *m)
{
	switch (m->type) {
	case E_MUTEX_SIMPLE:
		return pthread_mutex_unlock(&m->mutex);
	case E_MUTEX_REC:
		if (pthread_mutex_lock(&m->mutex) == -1)
			return -1;
		g_assert(m->owner == pthread_self());

		m->depth--;
		if (m->depth == 0) {
			m->owner = ~0;
			if (m->waiters > 0)
				pthread_cond_signal(&m->cond);
		}
		return pthread_mutex_unlock(&m->mutex);
	}

	errno = EINVAL;
	return -1;
}

#ifdef STANDALONE
EMsgPort *server_port;


void *fdserver(void *data)
{
	int fd;
	EMsg *msg;
	int id = (int)data;
	fd_set rfds;

	fd = e_msgport_fd(server_port);

	while (1) {
		int count = 0;

		printf("server %d: waiting on fd %d\n", id, fd);
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		select(fd+1, &rfds, NULL, NULL, NULL);
		printf("server %d: Got async notification, checking for messages\n", id);
		while ((msg = e_msgport_get(server_port))) {
			printf("server %d: got message\n", id);
			sleep(1);
			printf("server %d: replying\n", id);
			e_msgport_reply(msg);
			count++;
		}
		printf("server %d: got %d messages\n", id, count);
	}
}

void *server(void *data)
{
	EMsg *msg;
	int id = (int)data;

	while (1) {
		printf("server %d: waiting\n", id);
		msg = e_msgport_wait(server_port);
		msg = e_msgport_get(server_port);
		if (msg) {
			printf("server %d: got message\n", id);
			sleep(1);
			printf("server %d: replying\n", id);
			e_msgport_reply(msg);
		} else {
			printf("server %d: didn't get message\n", id);
		}
	}
}

void *client(void *data)
{
	EMsg *msg;
	EMsgPort *replyport;
	int i;

	replyport = e_msgport_new();
	msg = g_malloc0(sizeof(*msg));
	msg->reply_port = replyport;
	for (i=0;i<10;i++) {
		/* synchronous operation */
		printf("client: sending\n");
		e_msgport_put(server_port, msg);
		printf("client: waiting for reply\n");
		e_msgport_wait(replyport);
		e_msgport_get(replyport);
		printf("client: got reply\n");
	}

	printf("client: sleeping ...\n");
	sleep(2);
	printf("client: sending multiple\n");

	for (i=0;i<10;i++) {
		msg = g_malloc0(sizeof(*msg));
		msg->reply_port = replyport;
		e_msgport_put(server_port, msg);
	}

	printf("client: receiving multiple\n");
	for (i=0;i<10;i++) {
		e_msgport_wait(replyport);
		msg = e_msgport_get(replyport);
		g_free(msg);
	}

	printf("client: done\n");
}

int main(int argc, char **argv)
{
	pthread_t serverid, clientid;

	g_thread_init(NULL);

	server_port = e_msgport_new();

	/*pthread_create(&serverid, NULL, server, (void *)1);*/
	pthread_create(&serverid, NULL, fdserver, (void *)1);
	pthread_create(&clientid, NULL, client, NULL);

	sleep(60);

	return 0;
}
#endif
