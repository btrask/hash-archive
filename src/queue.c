// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <async/async.h>
#include <async/http/HTTP.h>
#include "util/hash.h"
#include "util/strext.h"
#include "db.h"
#include "page.h"

int url_fetch(strarg_t const URL, strarg_t const client, int *const outstatus, HTTPHeadersRef *const outheaders, hasher_t **const outhasher);

#define HXTimeIDQueuedURLAndClientKeyPack(...)
#define HXTimeIDQueuedURLAndClientKeyUnpack(...)
#define HXTimeIDQueuedURLAndClientRange2(...)

// TODO
DB_env *shared_db = NULL;
int hx_db_open(DB_env **const out) {
	assert(out);
	async_pool_enter(NULL);
	*out = shared_db;
	return 0;
}
void hx_db_close(DB_env **const in) {
	assert(in);
	DB_env *db = *in; *in = NULL;
	if(!db) return;
	db = NULL;
	async_pool_leave(NULL);
}
char const *hx_strerror(int const rc) {
	return NULL; // TODO
}



static uint64_t current_id = 0; // TODO



static uint64_t latest_time = 0;
static uint64_t latest_id = 0;
static async_mutex_t latest_lock[1];

static int queue_peek(uint64_t *const outtime, uint64_t *const outid, char *const outURL, size_t const urlmax, char *const outclient, size_t const clientmax) {
	assert(outtime);
	assert(outid);
	assert(outURL);
	assert(urlmax > 0);
	assert(outclient);
	assert(clientmax > 0);

	DB_env *db = NULL;
	DB_txn *txn = NULL;
	DB_cursor *cursor = NULL;
	DB_range range[1];
	DB_val key[1];
	strarg_t URL, client;
	int rc = 0;

	async_mutex_lock(latest_lock);
	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(rc < 0) goto cleanup;
	rc = db_txn_cursor(txn, &cursor);
	if(rc < 0) goto cleanup;
	HXTimeIDQueuedURLAndClientRange2(range, latest_time, latest_id);
	rc = db_cursor_firstr(cursor, range, key, NULL, +1);
	if(rc < 0) goto cleanup;

	HXTimeIDQueuedURLAndClientKeyUnpack(key, outtime, outid, &URL, &client);
	URL = NULL; // TODO
	client = NULL; // TODO
	strlcpy(outURL, URL ? URL : "", urlmax);
	strlcpy(outclient, client ? client : "", clientmax);
	latest_time = *outtime;
	latest_id = *outid;
cleanup:
	cursor = NULL;
	db_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	async_mutex_unlock(latest_lock);
	return rc;
}
static int queue_remove(DB_txn *const txn, uint64_t const time, uint64_t const id, strarg_t const URL, strarg_t const client) {
	assert(time);
	assert(id);
	assert(URL);
	assert(client);
	DB_val key[1];
	HXTimeIDQueuedURLAndClientKeyPack(key, time, id, URL, client);
	return db_del(txn, key, 0); // DB_NOOVERWRITE_FAST
}


int response_add(DB_txn *const txn, uint64_t const time, strarg_t const URL, int const status, strarg_t const content_type, uint64_t const content_length, hasher_t *const hasher) {
	return -1; // TODO
}

int queue_add(DB_txn *const txn, uint64_t const time, strarg_t const URL, strarg_t const client) {
	assert(time);
	assert(URL);
	assert(client);
	DB_val key[1];
	HXTimeIDQueuedURLAndClientKeyPack(key, time, current_id++, URL, client);
	return db_put(txn, key, NULL, 0); // DB_NOOVERWRITE_FAST
}
void queue_work(void) {
	uint64_t time;
	uint64_t id;
	char URL[URI_MAX];
	char client[255+1];
	uint64_t now;
	int status;
	HTTPHeadersRef headers = NULL;
	strarg_t type = NULL;
	uint64_t length;
	hasher_t *hasher = NULL;
	DB_env *db = NULL;
	DB_txn *txn = NULL;
	int rc;
	for(;;) {
		rc = queue_peek(&time, &id, URL, sizeof(URL), client, sizeof(client));
		if(DB_NOTFOUND == rc) {
			// TODO: Wait for event.
			continue;
		}
		if(rc < 0) break;

		now = uv_hrtime() / 1e9;
		rc = url_fetch(URL, client, &status, &headers, &hasher);
		if(rc < 0) break;

		type = HTTPHeadersGet(headers, "Content-Type");
		length = 0; // TODO

		rc = hx_db_open(&db);
		rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
		rc = queue_remove(txn, time, id, URL, client);
		rc = response_add(txn, now, URL, status, type, length, hasher);
		rc = db_txn_commit(txn); txn = NULL;
		hx_db_close(&db);
	}
	alogf("Worker terminated: %s\n", hx_strerror(rc));
}

