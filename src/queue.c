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

int url_fetch(strarg_t const URL, strarg_t const client, struct response *const out);


static uint64_t current_id = 0;
static async_mutex_t id_lock[1];

static uint64_t latest_time = 0;
static uint64_t latest_id = 0;
static async_mutex_t latest_lock[1];
static async_cond_t latest_cond[1];


// TODO: Define static async_x_t initializers
void queue_init(void) {
	async_mutex_init(id_lock, 0);
	async_mutex_init(latest_lock, 0);
	async_cond_init(latest_cond, 0);
}


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

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(rc < 0) goto cleanup;
	rc = db_txn_cursor(txn, &cursor);
	if(rc < 0) goto cleanup;
	HXTimeIDQueuedURLAndClientRange0(range);
	DB_VAL_STORAGE(key, DB_VARINT_MAX*3)
	db_bind_uint64(key, HXTimeIDQueuedURLAndClient);
	db_bind_uint64(key, latest_time);
	db_bind_uint64(key, latest_id+1);
	DB_VAL_STORAGE_VERIFY(key);
	rc = db_cursor_seekr(cursor, range, key, NULL, +1);
	if(rc < 0) goto cleanup;

	HXTimeIDQueuedURLAndClientKeyUnpack(key, txn, outtime, outid, &URL, &client);
	strlcpy(outURL, URL ? URL : "", urlmax);
	strlcpy(outclient, client ? client : "", clientmax);
	latest_time = *outtime;
	latest_id = *outid;
cleanup:
	cursor = NULL;
	db_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	return rc;
}
static int queue_remove(DB_txn *const txn, uint64_t const time, uint64_t const id, strarg_t const URL, strarg_t const client) {
	assert(time);
	assert(id);
	assert(URL);
	assert(client);
	char surt[URI_MAX];
	int rc = url_normalize_surt(URL, surt, sizeof(surt));
	if(rc < 0) goto cleanup;

	DB_val fwd_key[1];
	HXTimeIDQueuedURLAndClientKeyPack(fwd_key, txn, time, id, URL, client);
	rc = db_del(txn, fwd_key, 0);
	if(rc < 0) goto cleanup;

	DB_val rev_key[1];
	HXQueuedURLSurtAndTimeIDKeyPack(rev_key, txn, surt, time, id);
	rc = db_del(txn, rev_key, 0);
	if(rc < 0) goto cleanup;
cleanup:
	return rc;
}



int queue_add(uint64_t const time, strarg_t const URL, strarg_t const client) {
	assert(time);
	assert(URL);
	assert(client);
	char surt[URI_MAX];
	DB_env *db = NULL;
	DB_txn *txn = NULL;
	DB_cursor *cursor = NULL;
	int rc = 0;

	rc = url_normalize_surt(URL, surt, sizeof(surt));
	if(rc < 0) goto cleanup;

	async_mutex_lock(id_lock);
	uint64_t const id = ++current_id;
	async_mutex_unlock(id_lock);

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(rc < 0) goto cleanup;
	rc = db_txn_cursor(txn, &cursor);
	if(rc < 0) goto cleanup;

	DB_val chk_key[1];
	DB_range range_queued[1];
	HXQueuedURLSurtAndTimeIDRange1(range_queued, txn, surt);
	rc = db_cursor_firstr(cursor, range_queued, chk_key, NULL, -1);
	if(rc >= 0) rc = DB_KEYEXIST;
	if(DB_NOTFOUND != rc) goto cleanup;

	DB_range range_crawled[1];
	HXURLSurtAndTimeIDRange1(range_crawled, txn, surt);
	rc = db_cursor_firstr(cursor, range_crawled, chk_key, NULL, -1);
	if(rc >= 0) {
		strarg_t x;
		uint64_t ltime, lid;
		HXURLSurtAndTimeIDKeyUnpack(chk_key, txn, &x, &ltime, &lid);
		assert(0 == strcmp(x, surt));
		rc = ltime+CRAWL_DELAY_SECONDS < time ?
			DB_NOTFOUND : DB_KEYEXIST;
	}
	if(DB_NOTFOUND != rc) goto cleanup;

	DB_val fwd_key[1];
	HXTimeIDQueuedURLAndClientKeyPack(fwd_key, txn, time, id, URL, client);
	rc = db_put(txn, fwd_key, NULL, 0); // DB_NOOVERWRITE_FAST
	if(rc < 0) goto cleanup;

	DB_val rev_key[1];
	HXQueuedURLSurtAndTimeIDKeyPack(rev_key, txn, surt, time, id);
	rc = db_put(txn, rev_key, NULL, 0);
	if(rc < 0) goto cleanup;

	rc = db_txn_commit(txn); txn = NULL;
	if(rc < 0) goto cleanup;
	hx_db_close(&db);

	alogf("enqueued %s (%s)\n", URL, hx_strerror(rc));
	async_cond_broadcast(latest_cond);
cleanup:
	cursor = NULL;
	db_txn_abort(txn); txn = NULL;
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

	DB_env *db = NULL;
	DB_txn *txn = NULL;
	int rc = 0;

	async_mutex_lock(latest_lock);
	for(;;) {
		rc = queue_peek(&then, &old_id, URL, sizeof(URL), client, sizeof(client));
		if(DB_NOTFOUND != rc) break;
		rc = async_cond_wait(latest_cond, latest_lock);
		if(rc < 0) break;
	}
	async_mutex_unlock(latest_lock);
	if(rc < 0) goto cleanup;

	alogf("fetching %s\n", URL);

	async_mutex_lock(id_lock);
	new_id = current_id++;
	async_mutex_unlock(id_lock);
	rc = url_fetch(URL, client, res);
	if(rc < 0) goto cleanup;

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
	if(rc < 0) goto cleanup;
	rc = queue_remove(txn, then, old_id, URL, client);
	if(rc < 0) goto cleanup;
	rc = hx_response_add(txn, res, new_id);
	if(rc < 0) goto cleanup;
	rc = db_txn_commit(txn); txn = NULL;
	if(rc < 0) goto cleanup;

cleanup:
	db_txn_abort(txn); txn = NULL;
	hx_db_close(&db);

	if(rc >= 0) return;

	alogf("Worker error: %s\n", hx_strerror(rc));
	async_sleep(1000*5);
}
void queue_work_loop(void *ignored) {
	for(;;) queue_work();
}

