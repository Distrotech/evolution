
#ifndef _E_MSGPORT_H
#define _E_MSGPORT_H

/* double-linked list yeah another one, deal */
typedef struct _EDListNode {
	struct _EDListNode *next;
	struct _EDListNode *prev;
} EDListNode;

typedef struct _EDList {
	struct _EDListNode *head;
	struct _EDListNode *tail;
	struct _EDListNode *tailpred;
} EDList;

void e_dlist_init(EDList *v);
EDListNode *e_dlist_addhead(EDList *l, EDListNode *n);
EDListNode *e_dlist_addtail(EDList *l, EDListNode *n);
EDListNode *e_dlist_remove(EDListNode *n);
EDListNode *e_dlist_remhead(EDList *l);
EDListNode *e_dlist_remtail(EDList *l);
int e_dlist_empty(EDList *l);

/* message ports - a simple inter-thread 'ipc' primitive */
/* opaque handle */
typedef struct _EMsgPort EMsgPort;

/* header for any message */
typedef struct _EMsg {
	EDListNode ln;
	EMsgPort *reply_port;
} EMsg;

EMsgPort *e_msgport_new(void);
void e_msgport_destroy(EMsgPort *mp);
/* get a fd that can be used to wait on the port asynchronously */
int e_msgport_fd(EMsgPort *mp);
void e_msgport_put(EMsgPort *mp, EMsg *msg);
EMsg *e_msgport_wait(EMsgPort *mp);
EMsg *e_msgport_get(EMsgPort *mp);
void e_msgport_reply(EMsg *msg);

/* sigh, another mutex interface, this one allows different mutex types, portably */
typedef struct _EMutex EMutex;

typedef enum _e_mutex_t {
	E_MUTEX_SIMPLE,		/* == pthread_mutex */
	E_MUTEX_REC,		/* recursive mutex */
} e_mutex_t;

EMutex *e_mutex_new(e_mutex_t type);
int e_mutex_destroy(EMutex *m);
int e_mutex_lock(EMutex *m);
int e_mutex_unlock(EMutex *m);

#endif
