// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <async/async.h>
#include <async/http/HTTP.h>
#include "util/hash.h"
#include "util/strext.h"
#include "util/url.h"
#include "db.h"
#include "page.h"
#include "errors.h"
#include "config.h"
#include "queue.h"

// fetch.c
int url_fetch(strarg_t const URL, strarg_t const client, struct response *const out);


static uint64_t current_id = 0;
static async_mutex_t id_lock[1];

static uint64_t work_time = 0;
static uint64_t work_id = 0;
static async_mutex_t work_lock[1];
static async_cond_t work_cond[1];

static async_mutex_t wait_lock[1];
static async_cond_t wait_cond[1];

// TODO: Define static async_x_t initializers
void queue_init(void) {
	async_mutex_init(id_lock, 0);
	async_mutex_init(work_lock, 0);
	async_cond_init(work_cond, 0);
	async_mutex_init(wait_lock, 0);
	async_cond_init(wait_cond, 0);
}


void queue_log(size_t const n) {
	KVS_env *db = NULL;
	KVS_txn *txn = NULL;
	KVS_cursor *cursor = NULL;
	KVS_range range[1];
	KVS_val key[1];
	strarg_t URL, client;
	int rc = 0;

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = kvs_txn_begin(db, NULL, KVS_RDONLY, &txn);
	if(rc < 0) goto cleanup;
	rc = kvs_txn_cursor(txn, &cursor);
	if(rc < 0) goto cleanup;

	HXTimeIDQueuedURLAndClientRange0(range);
	rc = kvs_cursor_firstr(cursor, range, key, NULL, +1);
	size_t i = 0;
	for(; i < n; i++) {
		if(KVS_NOTFOUND == rc) break;
		if(rc < 0) goto cleanup;

		uint64_t time = 0;
		uint64_t id = 0;
		strarg_t URL = NULL;
		strarg_t client = NULL;
		HXTimeIDQueuedURLAndClientKeyUnpack(key, txn, &time, &id, &URL, &client);
		alogf("Queue %zu (%llu): '%s' for '%s'", i+1, (unsigned long long)time, URL, client);

		rc = kvs_cursor_nextr(cursor, range, key, NULL, +1);
	}

	size_t count = 0;
	rc = kvs_countr(txn, range, &count);
	alogf("Logged %zu of %zu queued URLs", i, count);

cleanup:
	cursor = NULL;
	kvs_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	if(rc) {
		alogf("Queue log error %s", kvs_strerror(rc));
	}
}


static int queue_peek(uint64_t *const outtime, uint64_t *const outid, char *const outURL, size_t const urlmax, char *const outclient, size_t const clientmax) {
	assert(outtime);
	assert(outid);
	assert(outURL);
	assert(urlmax > 0);
	assert(outclient);
	assert(clientmax > 0);

	KVS_env *db = NULL;
	KVS_txn *txn = NULL;
	KVS_cursor *cursor = NULL;
	KVS_range range[1];
	KVS_val key[1];
	strarg_t URL, client;
	int rc = 0;

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = kvs_txn_begin(db, NULL, KVS_RDONLY, &txn);
	if(rc < 0) goto cleanup;
	rc = kvs_txn_cursor(txn, &cursor);
	if(rc < 0) goto cleanup;
	HXTimeIDQueuedURLAndClientRange0(range);
	KVS_VAL_STORAGE(key, KVS_VARINT_MAX*3)
	kvs_bind_uint64(key, HXTimeIDQueuedURLAndClient);
	kvs_bind_uint64(key, work_time);
	kvs_bind_uint64(key, work_id+1);
	KVS_VAL_STORAGE_VERIFY(key);
	rc = kvs_cursor_seekr(cursor, range, key, NULL, +1);
	if(rc < 0) goto cleanup;

	HXTimeIDQueuedURLAndClientKeyUnpack(key, txn, outtime, outid, &URL, &client);
	strlcpy(outURL, URL ? URL : "", urlmax);
	strlcpy(outclient, client ? client : "", clientmax);
	work_time = *outtime;
	work_id = *outid;
cleanup:
	cursor = NULL;
	kvs_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	return rc;
}
static int queue_remove(KVS_txn *const txn, uint64_t const time, uint64_t const id, strarg_t const URL, strarg_t const client) {
	assert(time);
	assert(id);
	assert(URL);
	assert(client);
	char surt[URI_MAX];
	int rc = url_normalize_surt(URL, surt, sizeof(surt));
	if(rc < 0) goto cleanup;

	KVS_val fwd_key[1];
	HXTimeIDQueuedURLAndClientKeyPack(fwd_key, txn, time, id, URL, client);
	rc = kvs_del(txn, fwd_key, 0);
	if(rc < 0) goto cleanup;

	KVS_val rev_key[1];
	HXQueuedURLSurtAndTimeIDKeyPack(rev_key, txn, surt, time, id);
	rc = kvs_del(txn, rev_key, 0);
	if(rc < 0) goto cleanup;
cleanup:
	return rc;
}



int queue_add(uint64_t const time, strarg_t const URL, strarg_t const client) {
	assert(time);
	assert(URL);
	assert(client);
	char surt[URI_MAX];
	KVS_env *db = NULL;
	KVS_txn *txn = NULL;
	KVS_cursor *cursor = NULL;
	int rc = 0;

	rc = url_normalize_surt(URL, surt, sizeof(surt));
	if(rc < 0) goto cleanup;

	async_mutex_lock(id_lock);
	uint64_t const id = ++current_id;
	async_mutex_unlock(id_lock);

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = kvs_txn_begin(db, NULL, KVS_RDWR, &txn);
	if(rc < 0) goto cleanup;
	rc = kvs_txn_cursor(txn, &cursor);
	if(rc < 0) goto cleanup;

	KVS_val chk_key[1];
	KVS_range range_queued[1];
	HXQueuedURLSurtAndTimeIDRange1(range_queued, txn, surt);
	rc = kvs_cursor_firstr(cursor, range_queued, chk_key, NULL, -1);
	if(rc >= 0) goto cleanup; // If it's already queued, return success.
	if(KVS_NOTFOUND != rc) goto cleanup;

	KVS_range range_crawled[1];
	HXURLSurtAndTimeIDRange1(range_crawled, txn, surt);
	rc = kvs_cursor_firstr(cursor, range_crawled, chk_key, NULL, -1);
	if(rc >= 0) {
		strarg_t x;
		uint64_t ltime, lid;
		HXURLSurtAndTimeIDKeyUnpack(chk_key, txn, &x, &ltime, &lid);
		assert(0 == strcmp(x, surt));
		rc = ltime+CONFIG_CRAWL_DELAY_SECONDS < time ?
			KVS_NOTFOUND : KVS_KEYEXIST;
	}
	if(KVS_NOTFOUND != rc) goto cleanup;

	KVS_val fwd_key[1];
	HXTimeIDQueuedURLAndClientKeyPack(fwd_key, txn, time, id, URL, client);
	rc = kvs_put(txn, fwd_key, NULL, 0); // KVS_NOOVERWRITE_FAST
	if(rc < 0) goto cleanup;

	KVS_val rev_key[1];
	HXQueuedURLSurtAndTimeIDKeyPack(rev_key, txn, surt, time, id);
	rc = kvs_put(txn, rev_key, NULL, 0);
	if(rc < 0) goto cleanup;

	rc = kvs_txn_commit(txn); txn = NULL;
	if(rc < 0) goto cleanup;
	hx_db_close(&db);

	alogf("Enqueued %s (%s)\n", URL, hx_strerror(rc));
	async_cond_broadcast(work_cond);
cleanup:
	cursor = NULL;
	kvs_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	return rc;
}
int queue_timedwait(uint64_t const time, strarg_t const URL, uint64_t const future) {
	KVS_env *db = NULL;
	KVS_txn *txn = NULL;
	int rc = 0;
	async_mutex_lock(wait_lock);
	for(;;) {
		// It was tempting to use double-checked locking
		// or something even fancier, but for now it doesn't
		// seem worth it.
		uint64_t ltime, lid;
		rc = hx_db_open(&db);
		if(rc < 0) break;
		rc = kvs_txn_begin(db, NULL, KVS_RDONLY, &txn);
		if(rc < 0) break;
		rc = hx_get_latest(URL, txn, &ltime, &lid);
		kvs_txn_abort(txn); txn = NULL;
		hx_db_close(&db);
		if(rc >= 0) {
			if(ltime+CONFIG_CRAWL_DELAY_SECONDS >= time) break;
			rc = KVS_NOTFOUND;
		}
		if(KVS_NOTFOUND != rc) break;
		rc = async_cond_timedwait(wait_cond, wait_lock, future);
		if(rc < 0) break;
	}
	async_mutex_unlock(wait_lock);
	kvs_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	return rc;
}

static void queue_work(void) {
	uint64_t then;
	uint64_t old_id;
	char URL[URI_MAX];
	char client[255+1];

	struct response res[1];
	uint64_t new_id;

	KVS_env *db = NULL;
	KVS_txn *txn = NULL;
	int rc = 0;

	async_mutex_lock(work_lock);
	for(;;) {
		rc = queue_peek(&then, &old_id, URL, sizeof(URL), client, sizeof(client));
		if(KVS_NOTFOUND != rc) break;
		rc = async_cond_wait(work_cond, work_lock);
		if(rc < 0) break;
	}
	async_mutex_unlock(work_lock);
	if(rc < 0) goto cleanup;

	alogf("fetching %s\n", URL);

	async_mutex_lock(id_lock);
	new_id = current_id++;
	async_mutex_unlock(id_lock);
	rc = url_fetch(URL, client, res);
	if(rc < 0) goto cleanup;

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = kvs_txn_begin(db, NULL, KVS_RDWR, &txn);
	if(rc < 0) goto cleanup;
	rc = queue_remove(txn, then, old_id, URL, client);
	if(rc < 0) goto cleanup;
	rc = hx_response_add(txn, res, new_id);
	if(rc < 0) goto cleanup;
	rc = kvs_txn_commit(txn); txn = NULL;
	if(rc < 0) goto cleanup;

cleanup:
	kvs_txn_abort(txn); txn = NULL;
	hx_db_close(&db);

	if(rc < 0) {
		alogf("Worker error: %s\n", hx_strerror(rc));
		async_sleep(1000*5);
		return;
	}

	async_cond_broadcast(wait_cond);
}
void queue_work_loop(void *ignored) {
	for(;;) queue_work();
}

