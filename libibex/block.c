/*
  an extensible array mechanism with fixed size blocks

  backed by a cache
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <glib.h>

#include "block.h"

#define d(x)

#define BLOCK_SIZE (512)
#define CACHE_SIZE 1024		/* blocks in disk cache */
				/* total cache size = block size * cache size */

/* root block */
struct _root {
	char version[4];

	blockid_t free;		/* list of free blocks */
	blockid_t roof;		/* top of allocated space, everything below is in a free or used list */

	blockid_t index;	/* root of 'index' blocks */
	blockid_t names;	/* root of 'name' blocks */
};

/* key data for each index entry */
struct _idx_key {
	blockid_t root;
	int keyoffset;
};

/* disk structure for blocks */
struct _block {
	blockid_t next;		/* next block */
	guint32 used;		/* number of elements used */
	union {
		struct _idx_key keys[(BLOCK_SIZE-8)/sizeof(struct _idx_key)];
		char keydata[BLOCK_SIZE-8]; /* key data */
		nameid_t data[(BLOCK_SIZE-8)/4]; /* references */
	} block_u;
};
#define bl_data block_u.data
#define bl_keys block_u.keys
#define bl_keydata block_u.keydata

/* custom list structure, for a simple/efficient cache */
struct _listnode {
	struct _listnode *next;
	struct _listnode *prev;
};
struct _list {
	struct _listnode *head;
	struct _listnode *tail;
	struct _listnode *tailpred;
};

/* in-memory structure for block cache */
struct _memblock {
	struct _memblock *next;
	struct _memblock *prev;

	blockid_t block;
	int flags;

	struct _block data;
};
#define BLOCK_DIRTY (1<<0)

struct _memcache {
	struct _list nodes;
	int count;		/* nodes in cache */

	GHashTable *index;	/* blockid->memblock mapping */
	int fd;			/* file fd */

	GHashTable *index_keys;	/* key->memidx mapping */
};

/* in-memory structure for index table */
struct _memidx {
	blockid_t block;	/* block containing this index item */
	blockid_t root;		/* root of the list of index contents */
	int keyid;		/* id of this index item  this could be removed ... saving 4 bytes per word */
	char key[1];		/* key data follows */
};


int load_keys(struct _memcache *, blockid_t head);

/* for implementing an LRU cache */

static void list_new(struct _list *v)
{
	v->head = (struct _listnode *)&v->tail;
	v->tail = 0;
	v->tailpred = (struct _listnode *)&v->head;
}

static struct _listnode *list_addhead(struct _list *l, struct _listnode *n)
{
	n->next = l->head;
	n->prev = (struct _listnode *)&l->head;
	l->head->prev = n;
	l->head = n;
	return n;
}

static struct _listnode *list_addtail(struct _list *l, struct _listnode *n)
{
	n->next = (struct _listnode *)&l->tail;
	n->prev = l->tailpred;
	l->tailpred->next = n;
	l->tailpred = n;
	return n;
}

static struct _listnode *list_remove(struct _listnode *n)
{
	n->next->prev = n->prev;
	n->prev->next = n->next;
	return n;
}

static struct _memblock *
memblock_addr(struct _block *block)
{
	return (struct _memblock *)(((char *)block) - G_STRUCT_OFFSET(struct _memblock, data));
}

void
dirty_block(struct _block *block)
{
	memblock_addr(block)->flags |= BLOCK_DIRTY;
}

void
sync_block(struct _memcache *block_cache, struct _memblock *memblock)
{
	printf("\nsyncing block %d\n", memblock->block);
	lseek(block_cache->fd, memblock->block, SEEK_SET);
	if (write(block_cache->fd, &memblock->data, sizeof(memblock->data)) != -1) {
		memblock->flags &= ~BLOCK_DIRTY;
	}
}

void
sync_cache(struct _memcache *block_cache)
{
	struct _memblock *memblock;

	memblock = (struct _memblock *)block_cache->nodes.head;
	while (memblock->next) {
		if (memblock->flags & BLOCK_DIRTY) {
			sync_block(block_cache, memblock);
		}
		memblock = memblock->next;
	}
}


struct _block *
read_block(struct _memcache *block_cache, blockid_t blockid)
{
	struct _memblock *memblock;
	struct _block *block;

	/*g_assert(blockid < 1000*1024);*/

	memblock = g_hash_table_lookup(block_cache->index, (void *)blockid);
	if (memblock) {
		d(printf("foudn blockid in cache %d\n", blockid));
		/* 'access' page */
		list_remove((struct _listnode *)memblock);
		list_addtail(&block_cache->nodes, (struct _listnode *)memblock);
		return &memblock->data;
	}
	d(printf("loading blockid from disk %d\n", blockid));
	memblock = g_malloc(sizeof(*memblock));
	memblock->block = blockid;
	memblock->flags = 0;
	lseek(block_cache->fd, blockid, SEEK_SET);
	read(block_cache->fd, &memblock->data, sizeof(memblock->data));
	list_addtail(&block_cache->nodes, (struct _listnode *)memblock);
	g_hash_table_insert(block_cache->index, (void *)blockid, memblock);
	if (block_cache->count >= CACHE_SIZE) {
		struct _memblock *old = (struct _memblock *)block_cache->nodes.head;
		d(printf("discaring cache block %d\n", old->block));
		g_hash_table_remove(block_cache->index, (void *)old->block);
		list_remove((struct _listnode *)old);
		if (old->flags & BLOCK_DIRTY) {
			sync_block(block_cache, old);
		}
		g_free(old);
	} else {
		block_cache->count++;
	}

	d(printf("  --- cached blocks : %d\n", block_cache->count));

	return &memblock->data;
}

struct _memcache *
block_cache_open(const char *name, int flags, int mode)
{
	struct _root *root;
	struct _memcache *block_cache = g_malloc0(sizeof(*block_cache));

	/* setup cache */
	list_new(&block_cache->nodes);
	block_cache->count = 0;
	block_cache->index = g_hash_table_new(g_direct_hash, g_direct_equal);
	block_cache->fd = open(name, flags, mode);

	block_cache->index_keys = g_hash_table_new(g_str_hash, g_str_equal);

	if (block_cache->fd == -1) {
		g_hash_table_destroy(block_cache->index);
		g_hash_table_destroy(block_cache->index_keys);
		g_free(block_cache);
		return NULL;
	}

	root = (struct _root *)read_block(block_cache, 0);
	if (root->roof == 0) {
		d(printf("Initialising superblock\n"));
		/* reset root data */
		memcpy(root->version, "ibx3", 4);
		root->roof = 1024;
		root->free = 0;
		root->index = 0;
		root->names = 0;
		dirty_block((struct _block *)root);
	}
	if (root->index)
		load_keys(block_cache, root->index);

	return block_cache;
}

void
block_cache_close(struct _memcache *block_cache)
{
	sync_cache(block_cache);
	close(block_cache->fd);

	/* free blocks */

	g_hash_table_destroy(block_cache->index);

	/* free memidx stuff */
	g_hash_table_destroy(block_cache->index_keys);

	g_free(block_cache);
}

void
free_block(struct _memcache *block_cache, blockid_t blockid)
{
	struct _root *root = (struct _root *)read_block(block_cache, 0);
	struct _block *block = read_block(block_cache, blockid);

	block->next = root->free;
	root->free = blockid;
	dirty_block((struct _block *)root);
	dirty_block((struct _block *)block);
}

blockid_t
get_block(struct _memcache *block_cache)
{
	struct _root *root = (struct _root *)read_block(block_cache, 0);
	struct _block *block;
	blockid_t head;

	if (root->free) {
		head = root->free;
		block = read_block(block_cache, head);
		root->free = block->next;
	} else {
		head = root->roof;
		root->roof += BLOCK_SIZE;
		block = read_block(block_cache, head);
	}
	d(printf("new block = %d\n", head));
	block->next = 0;
	block->used = 0;
	dirty_block(block);
	dirty_block((struct _block *)root);
	return head;
}

blockid_t
add_datum(struct _memcache *block_cache, blockid_t head, nameid_t data)
{
	struct _block *block = read_block(block_cache, head);
	struct _block *newblock;
	blockid_t new;

	g_assert(head != 0);

	d(printf("adding record %d to block %d (next = %d)\n", data, head, block->next));

	if (block->used < sizeof(block->bl_data)/sizeof(block->bl_data[0])) {
		d(printf("adding record into block %d  %d\n", head, data));
		block->bl_data[block->used] = data;
		block->used++;
		dirty_block(block);
		return head;
	} else {
		new = get_block(block_cache);
		newblock = read_block(block_cache, new);
		newblock->next = head;
		newblock->bl_data[0] = data;
		newblock->used = 1;
		d(printf("adding record into new %d  %d, next =%d\n", new, data, newblock->next));
		dirty_block(newblock);
		return new;
	}
}

blockid_t
remove_datum(struct _memcache *block_cache, blockid_t head, nameid_t data)
{
	blockid_t node = head;

	d(printf("removing %d from %d\n", data, head));
	while (node) {
		struct _block *block = read_block(block_cache, node);
		int i;

		for (i=0;i<block->used;i++) {
			if (block->bl_data[i] == data) {
				struct _block *start = read_block(block_cache, head);

				start->used--;
				block->bl_data[i] = start->bl_data[start->used];
				if (start->used == 0) {
					struct _root *root = (struct _root *)read_block(block_cache, 0);
					blockid_t new;

					d(printf("dropping block %d, new head = %d\n", head, start->next));
					new = start->next;
					start->next = root->free;
					root->free = head;
					head = new;
					dirty_block((struct _block *)root);
				}
				dirty_block(block);
				dirty_block(start);
				return head;
			}
		}
		node = block->next;
	}
	return head;
}

gboolean
find_datum(struct _memcache *block_cache, blockid_t head, nameid_t data)
{
	blockid_t node = head;

	d(printf("finding %d from %d\n", data, head));
	while (node) {
		struct _block *block = read_block(block_cache, node);
		int i;

		for (i=0;i<block->used;i++) {
			if (block->bl_data[i] == data) {
				return TRUE;
			}
		}
		node = block->next;
	}
	return FALSE;
}

blockid_t
add_datum_list(struct _memcache *block_cache, blockid_t head, GArray *data)
{
	struct _block *block = read_block(block_cache, head);
	struct _block *newblock;
	blockid_t new;
	int copied = 0;
	int left, space, tocopy;

	while (copied < data->len) {
		left = data->len - copied;
		space = sizeof(block->bl_data)/sizeof(block->bl_data[0]) - block->used;
		if (space) {
			tocopy = MIN(left, space);
			memcpy(block->bl_data+block->used, &g_array_index(data, blockid_t, copied), tocopy);
			block->used += tocopy;
			dirty_block(block);
		} else {
			new = get_block(block_cache);
			newblock = read_block(block_cache, new);
			newblock->next = head;
			tocopy = MIN(left, sizeof(block->bl_data)/sizeof(block->bl_data[0]));
			memcpy(newblock->bl_data, &g_array_index(data, blockid_t, copied), tocopy);
			newblock->used = tocopy;
			block = newblock;
			head = new;
			dirty_block(newblock);
		}
		copied += tocopy;
	}
	return head;
}

GArray *
get_datum(struct _memcache *block_cache, blockid_t head)
{
	GArray *result = g_array_new(0, 0, sizeof(id_t));

	while (head) {
		struct _block *block = read_block(block_cache, head);

		d(printf("getting data from block %d\n", head));

		g_array_append_vals(result, block->bl_data, block->used);
		head = block->next;
		d(printf("next = %d\n", head));
	}
	return result;
}

void

add_key_mem(struct _memcache *block_cache, const char *key, int keylen, blockid_t root, blockid_t block, int keyid)
{
	struct _memidx *memidx;

	d(printf("adding key %.*s\n", keylen, key));

	memidx = g_malloc(sizeof(*memidx)+keylen);
	memcpy(memidx->key, key, keylen);
	memidx->key[keylen] = '\0';
	memidx->root = root;
	memidx->block = block;
	memidx->keyid = keyid;
	g_hash_table_insert(block_cache->index_keys, memidx->key, memidx);
}

/* load all keys from the file into the memory index (hash table) */
int
load_keys(struct _memcache *block_cache, blockid_t head)
{
	while (head) {
		struct _block *index = read_block(block_cache, head);
		int i, offsetlast = sizeof(index->bl_keydata);

		for (i=0;i<index->used;i++) {
			int offset = index->bl_keys[i].keyoffset;
			add_key_mem(block_cache, &index->bl_keydata[offset],
				    offsetlast - offset, index->bl_keys[i].root, head, i);
			offsetlast = offset;
		}
		head = index->next;
	}
	return 0;
}

/* there is no way to remove keys, is that a problem? */
blockid_t
add_key(struct _memcache *block_cache, const char *key)
{
	struct _root *root = (struct _root *)read_block(block_cache, 0);
	struct _block *index;
	int keylen = strlen(key);
	int room;
	blockid_t new;

	d(printf("adding new key %s\n", key));

	if (root->index == 0) {
		root->index = get_block(block_cache);
		dirty_block((struct _block *)root);
	}
	index = read_block(block_cache, root->index);

	g_assert(keylen < sizeof(index->bl_keydata));

	if (index->used > 0) {
		room = ((void *)&index->bl_keydata[index->bl_keys[index->used-1].keyoffset]) -
			((void *)&index->bl_keys[index->used+1]);
		if (room < keylen) {
			blockid_t new = get_block(block_cache);
			struct _block *newblock = read_block(block_cache, new);
			
			newblock->next = root->index;
			root->index = new;
			index = newblock;
			dirty_block((struct _block *)root);
		}
	}

	d(printf("adding key %s to block %d\n", key, root->index));

	if (index->used == 0) {
		index->bl_keys[index->used].keyoffset = sizeof(index->bl_keydata)-keylen;
	} else {
		index->bl_keys[index->used].keyoffset = index->bl_keys[index->used-1].keyoffset - keylen;
	}

	memcpy(&index->bl_keydata[index->bl_keys[index->used].keyoffset], key, keylen);
	new = get_block(block_cache);
	index->bl_keys[index->used].root = new;

	add_key_mem(block_cache, key, keylen, new, root->index, index->used);
	index->used ++;
	dirty_block(index);
	return new;
}

blockid_t
key_to_block(struct _memcache *block_cache, const char *key)
{
	struct _memidx *memidx;

	memidx = g_hash_table_lookup(block_cache->index_keys, key);
	if (memidx) {
		d(printf("key block '%s' = %d\n", key, memidx->root));
		return memidx->root;
	} else {
		d(printf("key block '%s' = not found\n", key));
		return 0;
	}
}

int
update_key_root(struct _memcache *block_cache, const char *key, blockid_t root)
{
	struct _memidx *memidx;

	d(printf("updating key root %s = %d\n", key, root));
	memidx = g_hash_table_lookup(block_cache->index_keys, key);
	if (memidx) {
		struct _block *index = read_block(block_cache, memidx->block);
		d(printf("key is stored in block %d\n", memidx->block));
		if (index->bl_keys[memidx->keyid].root != root) {
			index->bl_keys[memidx->keyid].root = root;
			dirty_block(index);
		}
		memidx->root = root;
	}
}

GArray *
get_record(struct _memcache *block_cache, const char *key)
{
	blockid_t head;

	head = key_to_block(block_cache, key);

	/* handles the case of not-found (head == 0) */
	return get_datum(block_cache, head);
}

int
add_record(struct _memcache *block_cache, const char *key, nameid_t data)
{
	blockid_t head, new;

	d(printf("adding record %s = %d\n", key, data));

	head = key_to_block(block_cache, key);
	if (head == 0) {
		head = add_key(block_cache, key);
	}
	new = add_datum(block_cache, head, data);
	if (new != head)
		update_key_root(block_cache, key, new);
	return 0;
}

int
remove_record(struct _memcache *block_cache, const char *key, nameid_t data)
{
	blockid_t head, new;

	d(printf("adding record %s = %d\n", key, data));

	head = key_to_block(block_cache, key);
	if (head == 0) {
		return 0;
	}
	new = remove_datum(block_cache, head, data);
	if (new != head)
		update_key_root(block_cache, key, new);
	return 0;
}

gboolean
find_record(struct _memcache *block_cache, const char *key, nameid_t data)
{
	blockid_t head, new;

	d(printf("adding record %s = %d\n", key, data));

	head = key_to_block(block_cache, key);
	return find_datum(block_cache, head, data);
}

/* add a name indexed */
/* FIXME: cache this in memory */
int
add_indexed(struct _memcache *block_cache, nameid_t data)
{
	blockid_t head, new;
	struct _root *root = (struct _root *)read_block(block_cache, 0);

	d(printf("adding name %d\n", data));

	head = root->names;
	if (head == 0) {
		head = get_block(block_cache);
	}
	new = add_datum(block_cache, head, data);
	if (new != head) {
		root->names = new;
		dirty_block((struct _block *)root);
	}
	return 0;
}

struct _remove_data {
	struct _memcache *blocks;
	nameid_t id;
};

static void
remove_indexed_node(char *key, struct _memidx *memidx, struct _remove_data *data)
{
	d(printf("removing '%d' from '%s'\n", data->id, key));
	remove_record(data->blocks, key, data->id);
}

int
remove_indexed(struct _memcache *block_cache, nameid_t data)
{
	blockid_t head, new;
	struct _root *root = (struct _root *)read_block(block_cache, 0);
	struct _remove_data cbdata;
 
	d(printf("remvoing name %d\n", data));

	head = root->names;
	if (head == 0) {
		return 0;
	}
	new = remove_datum(block_cache, head, data);
	if (new != head) {
		root->names = new;
		dirty_block((struct _block *)root);
	}

	/* sigh, this basically scans the whole database, for occurances of data */
	cbdata.blocks = block_cache;
	cbdata.id = data;
	g_hash_table_foreach(block_cache->index_keys, (GHFunc)remove_indexed_node, &cbdata);
	return 0;
}

gboolean
find_indexed(struct _memcache *block_cache, nameid_t data)
{
	struct _root *root = (struct _root *)read_block(block_cache, 0);

	d(printf("finding name %d\n", data));
	return find_datum(block_cache, root->names, data);
}

#if 0
main()
{
	blockid_t node;
	int i;
	nameid_t data;
	GArray *contents;
	struct _memcache *bc;

	bc = block_cache_open("index.db", O_CREAT|O_RDWR, 0600);

	for (i=0;i<200;i++) {
		char name[16];
		int j;

		add_record(bc, "word", i);
		add_record(bc, "foo", i*2);
		if (i&1)
			add_record(bc, "blah", i);

		/* blow that cache right oiut of the air ... */
		for (j=0;j<300;j++) {
			sprintf(name, "x%d", j);
			add_record(bc, name, i);
		}
		
	}

	printf("read contents:\n");
	node = key_to_block(bc, "word");
	contents = get_datum(bc, node);
	for (i=0;i<contents->len;i++) {
		data = g_array_index(contents, nameid_t, i);
		printf(" %d", data);
	}
	printf("\n");

	printf("removing some items\n");
	node = key_to_block(bc, "word");
	for (i=13;i<200;i++) {
		remove_record(bc, "word", i);
	}

	node = key_to_block(bc, "word");
	contents = get_datum(bc, node);
	for (i=0;i<contents->len;i++) {
		data = g_array_index(contents, nameid_t, i);
		printf(" %d", data);
	}
	printf("\n");
	
	node = key_to_block(bc, "foo");
	contents = get_datum(bc, node);
	for (i=0;i<contents->len;i++) {
		data = g_array_index(contents, nameid_t, i);
		printf(" %d", data);
	}
	printf("\n");

	node = key_to_block(bc, "blah");
	contents = get_datum(bc, node);
	for (i=0;i<contents->len;i++) {
		data = g_array_index(contents, nameid_t, i);
		printf(" %d", data);
	}
	printf("\n");

	block_cache_close(bc);
}
#endif
