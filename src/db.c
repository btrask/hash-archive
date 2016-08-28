// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <async/async.h>
#include "util/url.h"
#include "db.h"

static DB_env *shared_db = NULL;

int hx_db_load(void) {
	if(shared_db) return 0;
	DB_env *db = NULL;
	int rc = 0;
	rc = db_env_create(&db);
	if(rc < 0) goto cleanup;
	rc = db_env_set_mapsize(db, 1024ull*1024*1024*64); // 64GB
	if(rc < 0) goto cleanup;
	rc = db_env_open(db, "/home/user/Desktop/test.db", 0, 0600);
	if(rc < 0) goto cleanup;
	shared_db = db; db = NULL;
cleanup:
	db_env_close(db); db = NULL;
	return rc;
}
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
	if(rc >= 0) return "No error";
	char const *x = db_strerror(rc);
	if(x) return x;
	return uv_strerror(rc);
}

int hx_response_add(DB_txn *const txn, struct response const *const res, uint64_t const id) {
	assert(txn);
	assert(res);
	char URL_surt[URI_MAX];
	int rc = url_normalize_surt(res->url, URL_surt, sizeof(URL_surt));
	if(rc < 0) return rc;

	// TODO: Signed varints would be more efficient.
	int64_t sstatus = 0xffff + res->status;
	db_assert(sstatus >= 0);

	DB_val res_key[1], res_val[1];
	HXTimeIDToResponseKeyPack(res_key, res->time, id);
	DB_VAL_STORAGE(res_val,
		DB_INLINE_MAX +
		DB_VARINT_MAX +
		DB_INLINE_MAX +
		DB_VARINT_MAX *
		DB_BLOB_MAX(HASH_DIGEST_MAX)*HASH_ALGO_MAX)
	db_bind_string(res_val, res->url, txn);
	db_bind_uint64(res_val, (uint64_t)sstatus);
	db_bind_string(res_val, res->type, txn);
	db_bind_uint64(res_val, res->length);
	for(size_t i = 0; i < numberof(res->digests); i++) {
		db_assert(res->digests[i].len <= hash_algo_digest_len(i));
		db_bind_uint64(res_val, res->digests[i].len);
		db_bind_blob(res_val, res->digests[i].buf, res->digests[i].len);
	}
	DB_VAL_STORAGE_VERIFY(res_val);
	rc = db_put(txn, res_key, res_val, DB_NOOVERWRITE_FAST);
	if(rc < 0) return rc;

	DB_val url_key[1];
	HXURLSurtAndTimeIDKeyPack(url_key, txn, URL_surt, res->time, id);
	rc = db_put(txn, url_key, NULL, DB_NOOVERWRITE_FAST);
	if(rc < 0) return rc;

	DB_val hash_key[1];
	for(size_t i = 0; i < numberof(res->digests); i++) {
		if(!res->digests[i].len) continue;
		assert(res->digests[i].len >= HX_HASH_INDEX_LEN);
		HXAlgoHashAndTimeIDKeyPack(hash_key, i, res->digests[i].buf, res->time, id);
		rc = db_put(txn, hash_key, NULL, DB_NOOVERWRITE_FAST);
		if(rc < 0) return rc;
	}

	return 0;
}

ssize_t hx_get_history(strarg_t const URL, struct response *const out, size_t const max) {
	assert(out);
	assert(max > 0);

	char surt[URI_MAX];
	int rc = url_normalize_surt(URL, surt, sizeof(surt));
	if(rc < 0) return rc;

	DB_env *db = NULL;
	DB_txn *txn = NULL;
	DB_cursor *cursor = NULL;
	size_t i = 0;

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(rc < 0) goto cleanup;
	rc = db_cursor_open(txn, &cursor);
	if(rc < 0) goto cleanup;

	DB_range range[1];
	DB_val key[1];
	HXURLSurtAndTimeIDRange1(range, txn, surt);
	rc = db_cursor_firstr(cursor, range, key, NULL, -1);
	if(rc < 0 && DB_NOTFOUND != rc) goto cleanup;
	for(; rc >= 0 && i < max; rc = db_cursor_nextr(cursor, range, key, NULL, -1)) {
		strarg_t surt;
		uint64_t time, id;
		HXURLSurtAndTimeIDKeyUnpack(key, txn, &surt, &time, &id);

		DB_val res_key[1], res_val[1];
		HXTimeIDToResponseKeyPack(res_key, time, id);
		rc = db_get(txn, res_key, res_val);
		if(rc < 0) goto cleanup;

		out[i].time = time;
		HXTimeIDToResponseValUnpack(res_val, txn, &out[i]);
		i++;
	}
	rc = 0;

cleanup:
	db_cursor_close(cursor); cursor = NULL;
	db_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	if(rc < 0) return rc;
	return i;
}
ssize_t hx_get_sources(hash_uri_t const *const obj, struct response *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	if(!obj) return DB_EINVAL;

	DB_env *db = NULL;
	DB_txn *txn = NULL;
	DB_cursor *cursor = NULL;
	int i = 0;
	int rc = 0;

	rc = hx_db_open(&db);
	if(rc < 0) goto cleanup;
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(rc < 0) goto cleanup;
	rc = db_cursor_open(txn, &cursor);
	if(rc < 0) goto cleanup;

	DB_range range[1];
	DB_val hash_key[1];
	HXAlgoHashAndTimeIDRange2(range, obj->algo, obj->buf, obj->len);
	rc = db_cursor_firstr(cursor, range, hash_key, NULL, -1);
	for(; rc >= 0 && i < max; rc = db_cursor_nextr(cursor, range, hash_key, NULL, -1)) {
		hash_algo algo;
		unsigned char const *hash;
		uint64_t time, id;
		HXAlgoHashAndTimeIDKeyUnpack(hash_key, &algo, &hash, &time, &id);

		DB_val res_key[1], res_val[1];
		HXTimeIDToResponseKeyPack(res_key, time, id);
		rc = db_get(txn, res_key, res_val);
		if(rc < 0) goto cleanup;

		out[i].time = time;
		HXTimeIDToResponseValUnpack(res_val, txn, &out[i]);

		// Our index is truncated so it can return spurrious matches.
		// Ensure the complete prefix matches.
		if(obj->len > out[i].digests[obj->algo].len) continue;
		if(0 != memcmp(out[i].digests[obj->algo].buf, obj->buf, obj->len)) continue;

		i++;
	}
	rc = 0;

cleanup:
	db_cursor_close(cursor); cursor = NULL;
	db_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	if(rc < 0) return rc;
	return i;
}

