// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <async/async.h>
#include "util/url.h"
#include "db.h"
#include "errors.h"
#include "config.h"

static int timeidcmp(uint64_t const t1, uint64_t const i1, uint64_t const t2, uint64_t const i2) {
	if(t1 > t2) return +1;
	if(t1 < t2) return -1;
	if(i1 > i2) return +1;
	if(i1 < i2) return -1;
	return 0;
}

static KVS_env *shared_db = NULL;

int hx_db_load(void) {
	if(shared_db) return 0;
	size_t mapsize = 1024ull*1024*1024*64; // 64GB
	KVS_env *db = NULL;
	int rc = 0;
	rc = kvs_env_create_base("leveldb", &db);
	if(rc < 0) goto cleanup;
	rc = kvs_env_set_config(db, KVS_CFG_MAPSIZE, &mapsize);
	if(rc < 0) goto cleanup;
	rc = kvs_env_open(db, CONFIG_DB_PATH, 0, 0600);
	if(rc < 0) goto cleanup;
	shared_db = db; db = NULL;
cleanup:
	kvs_env_close(db); db = NULL;
	return rc;
}
int hx_db_open(KVS_env **const out) {
	assert(out);
	async_pool_enter(NULL);
	*out = shared_db;
	return 0;
}
void hx_db_close(KVS_env **const in) {
	assert(in);
	KVS_env *db = *in; *in = NULL;
	if(!db) return;
	db = NULL;
	async_pool_leave(NULL);
}

int hx_response_add(KVS_txn *const txn, struct response const *const res, uint64_t const id) {
	assert(txn);
	assert(res);
	char URL_surt[URI_MAX];
	int rc = url_normalize_surt(res->url, URL_surt, sizeof(URL_surt));
	if(rc < 0) return rc;

	// TODO: Signed varints would be more efficient.
	int64_t sstatus = 0xffff + res->status;
	kvs_assert(sstatus >= 0);

	KVS_val res_key[1], res_val[1];
	HXTimeIDToResponseKeyPack(res_key, res->time, id);
	KVS_VAL_STORAGE(res_val,
		KVS_INLINE_MAX +
		KVS_VARINT_MAX +
		KVS_INLINE_MAX +
		KVS_VARINT_MAX *
		KVS_BLOB_MAX(HASH_DIGEST_MAX)*HASH_ALGO_MAX)
	kvs_bind_string(res_val, res->url, txn);
	kvs_bind_uint64(res_val, (uint64_t)sstatus);
	kvs_bind_string(res_val, res->type, txn);
	assert(0 == (uint64_t)UINT64_MAX+1);
	kvs_bind_uint64(res_val, res->length+1); // UINT64_MAX -> 0

	for(size_t i = 0; i < numberof(res->digests); i++) {
		size_t const len = res->digests[i].len;
		kvs_assert(len <= hash_algo_digest_len(i));
		kvs_bind_uint64(res_val, len);
		kvs_bind_blob(res_val, res->digests[i].buf, len);
	}

	KVS_VAL_STORAGE_VERIFY(res_val);
	rc = kvs_put(txn, res_key, res_val, KVS_NOOVERWRITE_FAST);
	if(rc < 0) return rc;

	KVS_val url_key[1];
	HXURLSurtAndTimeIDKeyPack(url_key, txn, URL_surt, res->time, id);
	rc = kvs_put(txn, url_key, NULL, KVS_NOOVERWRITE_FAST);
	if(rc < 0) return rc;

	KVS_val hash_key[1];
	for(size_t i = 0; i < numberof(res->digests); i++) {
		if(!res->digests[i].len) continue;
		assert(res->digests[i].len >= HX_HASH_INDEX_LEN);
		HXAlgoHashAndTimeIDKeyPack(hash_key, i, res->digests[i].buf, res->time, id);
		rc = kvs_put(txn, hash_key, NULL, KVS_NOOVERWRITE_FAST);
		if(rc < 0) return rc;
	}

	return 0;
}

static bool res_content_eq(struct response const *const a, struct response const *const b) {
	if(a == b) return true;
	if(!a || !b) return false;

	// Don't merge responses that aren't "OK".
	if(200 != a->status || 200 != b->status) return false;

	// We currently don't display types in our URI, so comparing them is confusing.
//	if(0 != strcmp(a->type, b->type)) return false;

	// If lengths are unknown (UINT64_MAX) this fails.
//	if(a->length != b->length) return false;

	// We only compare the prefix. For empty hashes this is zero which is good.
	size_t match = 0;
	for(size_t i = 0; i < HASH_ALGO_MAX; i++) {
		size_t const len = MIN(a->digests[i].len, b->digests[i].len);
		if(0 != memcmp(a->digests[i].buf, b->digests[i].buf, len)) return false;
		if(len >= 12) match++;
	}

	// We require at least one hash of at least 12 bytes having compared equal.
	if(0 == match) return false;

	return true;
}
static void res_merge_common_content(struct response *const responses, size_t const len) {
	for(size_t i = 1; i < len; i++) {
		bool const eq = res_content_eq(&responses[i-1], &responses[i]);
		if(!eq) continue;
		responses[i-1].next = &responses[i];
		responses[i].prev = &responses[i-1];
	}
}
static void res_merge_common_urls(struct response *const responses, size_t const len) {
	for(size_t i = 1; i < len; i++) {
		for(size_t j = i; j-- > 0;) {
			if(responses[j].next) continue;
			if(0 != strcmp(responses[j].url, responses[i].url)) continue;
			responses[j].next = &responses[i];
			responses[i].prev = &responses[j];
			break;
		}
	}
}

ssize_t hx_get_recent(struct response *const out, size_t const max) {
	assert(out);
	assert(max > 0);

	KVS_env *db = NULL;
	KVS_txn *txn = NULL;
	KVS_cursor *cursor = NULL;
	size_t i = 0;
	int rc = 0;

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = kvs_txn_begin(db, NULL, KVS_RDONLY, &txn);
	if(rc < 0) goto cleanup;
	rc = kvs_cursor_open(txn, &cursor);
	if(rc < 0) goto cleanup;

	KVS_range range[1];
	KVS_val key[1], val[1];
	HXTimeIDToResponseRange0(range);
	rc = kvs_cursor_firstr(cursor, range, key, val, -1);
	if(rc < 0 && KVS_NOTFOUND != rc) goto cleanup;
	for(; rc >= 0 && i < max; rc = kvs_cursor_nextr(cursor, range, key, val, -1)) {
		uint64_t time, id;
		HXTimeIDToResponseKeyUnpack(key, &time, &id);
		out[i].time = time;
		out[i].id = id;
		HXTimeIDToResponseValUnpack(val, txn, &out[i]);

		// Don't list failed responses.
		if(200 != out[i].status) continue;

		// Skip duplicate URLs.
		size_t j = 0;
		for(; j < i; j++) {
			if(0 == strcmp(out[j].url, out[i].url)) break;
		}
		if(j < i) continue;

		i++;
	}
	rc = 0;

cleanup:
	kvs_cursor_close(cursor); cursor = NULL;
	kvs_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	if(rc < 0) return rc;
	return i;
}
ssize_t hx_get_history(strarg_t const URL, struct response *const out, size_t const max) {
	assert(out);
	assert(max > 0);

	char surt[URI_MAX];
	int rc = url_normalize_surt(URL, surt, sizeof(surt));
	if(rc < 0) return rc;

	KVS_env *db = NULL;
	KVS_txn *txn = NULL;
	KVS_cursor *cursor = NULL;
	size_t i = 0;

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = kvs_txn_begin(db, NULL, KVS_RDONLY, &txn);
	if(rc < 0) goto cleanup;
	rc = kvs_cursor_open(txn, &cursor);
	if(rc < 0) goto cleanup;

	KVS_range range[1];
	KVS_val key[1];
	HXURLSurtAndTimeIDRange1(range, txn, surt);
	rc = kvs_cursor_firstr(cursor, range, key, NULL, -1);
	if(rc < 0 && KVS_NOTFOUND != rc) goto cleanup;
	for(; rc >= 0 && i < max; rc = kvs_cursor_nextr(cursor, range, key, NULL, -1)) {
		strarg_t surt;
		uint64_t time, id;
		HXURLSurtAndTimeIDKeyUnpack(key, txn, &surt, &time, &id);

		KVS_val res_key[1], res_val[1];
		HXTimeIDToResponseKeyPack(res_key, time, id);
		rc = kvs_get(txn, res_key, res_val);
		if(rc < 0) goto cleanup;

		out[i].time = time;
		out[i].id = id;
		HXTimeIDToResponseValUnpack(res_val, txn, &out[i]);
		i++;
	}
	rc = 0;
	res_merge_common_content(out, i);

cleanup:
	kvs_cursor_close(cursor); cursor = NULL;
	kvs_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	if(rc < 0) return rc;
	return i;
}
ssize_t hx_get_sources(hash_uri_t const *const obj, struct response *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	if(!obj) return KVS_EINVAL;

	KVS_env *db = NULL;
	KVS_txn *txn = NULL;
	KVS_cursor *cursor = NULL;
	int i = 0;
	int rc = 0;

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = kvs_txn_begin(db, NULL, KVS_RDONLY, &txn);
	if(rc < 0) goto cleanup;
	rc = kvs_cursor_open(txn, &cursor);
	if(rc < 0) goto cleanup;

	KVS_range range[1];
	KVS_val hash_key[1];
	HXAlgoHashAndTimeIDRange2(range, obj->algo, obj->buf, obj->len);
	rc = kvs_cursor_firstr(cursor, range, hash_key, NULL, -1);
	for(; rc >= 0 && i < max; rc = kvs_cursor_nextr(cursor, range, hash_key, NULL, -1)) {
		hash_algo algo;
		unsigned char const *hash;
		uint64_t time, id;
		HXAlgoHashAndTimeIDKeyUnpack(hash_key, &algo, &hash, &time, &id);

		KVS_val res_key[1], res_val[1];
		HXTimeIDToResponseKeyPack(res_key, time, id);
		rc = kvs_get(txn, res_key, res_val);
		if(rc < 0) goto cleanup;

		out[i].time = time;
		out[i].id = id;
		out[i].flags = 0;
		HXTimeIDToResponseValUnpack(res_val, txn, &out[i]);

		// Our index is truncated so it can return spurrious matches.
		// Ensure the complete prefix matches.
		if(obj->len > out[i].digests[obj->algo].len) continue;
		if(0 != memcmp(out[i].digests[obj->algo].buf, obj->buf, obj->len)) continue;

		uint64_t ltime, lid;
		rc = hx_get_latest(out[i].url, txn, &ltime, &lid);
		if(rc < 0) goto cleanup;
		int x = timeidcmp(ltime, lid, time, id);
		assert(x >= 0);
		if(0 == x) out[i].flags |= HX_RES_LATEST;

		i++;
	}
	rc = 0;
	res_merge_common_urls(out, i);

cleanup:
	kvs_cursor_close(cursor); cursor = NULL;
	kvs_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	if(rc < 0) return rc;
	return i;
}
ssize_t hx_get_times(uint64_t const time, uint64_t const id, int const dir, struct response *const out, size_t const max) {
	KVS_env *db = NULL;
	KVS_txn *txn = NULL;
	KVS_cursor *cursor = NULL;
	size_t i = 0;
	int rc;

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = kvs_txn_begin(db, NULL, KVS_RDONLY, &txn);
	if(rc < 0) goto cleanup;
	rc = kvs_cursor_open(txn, &cursor);
	if(rc < 0) goto cleanup;

	KVS_range range[1];
	KVS_val key[1], val[1];
	HXTimeIDToResponseRange0(range);
	HXTimeIDToResponseKeyPack(key, time, id);
	rc = kvs_cursor_seekr(cursor, range, key, val, dir);
	if(rc < 0 && KVS_NOTFOUND != rc) goto cleanup;
	for(; rc >= 0 && i < max; rc = kvs_cursor_nextr(cursor, range, key, val, dir)) {
		uint64_t xtime, xid;
		HXTimeIDToResponseKeyUnpack(key, &xtime, &xid);
		out[i].time = xtime;
		out[i].id = xid;
		HXTimeIDToResponseValUnpack(val, txn, &out[i]);
		i++;
	}
	rc = 0;

cleanup:
	kvs_cursor_close(cursor); cursor = NULL;
	kvs_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	if(rc < 0) return rc;
	return i;
}
int hx_get_latest(strarg_t const URL, KVS_txn *const txn, uint64_t *const time, uint64_t *const id) {
	assert(time);
	assert(id);
	KVS_cursor *cursor = NULL;
	char surt[URI_MAX];
	int rc = url_normalize_surt(URL, surt, sizeof(surt));
	if(rc < 0) goto cleanup;
	rc = kvs_txn_cursor(txn, &cursor);
	if(rc < 0) goto cleanup;
	KVS_range urls_range[1];
	KVS_val latest_key[1];
	HXURLSurtAndTimeIDRange1(urls_range, txn, surt);
	rc = kvs_cursor_firstr(cursor, urls_range, latest_key, NULL, -1);
	if(rc < 0) goto cleanup;
	strarg_t x;
	HXURLSurtAndTimeIDKeyUnpack(latest_key, txn, &x, time, id);
cleanup:
	cursor = NULL;
	return rc;
}

