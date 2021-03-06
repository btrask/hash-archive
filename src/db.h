// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <string.h>
#include <kvstore/kvs_schema.h>
#include "util/hash.h"
#include "common.h"

enum {
	// This is the latest response for this URL.
	// If there is a later response and their hashes differ,
	// then the current response is obsolete.
	HX_RES_LATEST = 1 << 0,
};

struct response {
	uint64_t time;
	uint64_t id;
	char url[URI_MAX];
	int status;
	char type[TYPE_MAX];
	uint64_t length;
	hash_digest_t digests[HASH_ALGO_MAX];
	struct response *next;
	struct response *prev;
	unsigned int flags;
};

int hx_db_load(void);
int hx_db_open(KVS_env **const out);
void hx_db_close(KVS_env **const in);

int hx_response_add(KVS_txn *const txn, struct response const *const res, uint64_t const id);

ssize_t hx_get_recent(struct response *const out, size_t const max);
ssize_t hx_get_history(strarg_t const URL, struct response *const out, size_t const max);
ssize_t hx_get_sources(hash_uri_t const *const obj, struct response *const out, size_t const max);
ssize_t hx_get_times(uint64_t const time, uint64_t const id, int const dir, struct response *const out, size_t const max);
int hx_get_latest(strarg_t const URL, KVS_txn *const txn, uint64_t *const time, uint64_t *const id);

enum {
	// 0-19 reserved.
	// Remember this is the permanent on-disk format.

	HXTimeIDToResponse = 20,
	HXURLSurtAndTimeID = 21,

	HXTimeIDQueuedURLAndClient = 30,
	HXQueuedURLSurtAndTimeID = 31,

	HXHashAndTimeID = 50, // Note: hashes truncated, not necessarily unique!
	// Add HASH_ALGO_XX to get per-algo table.
};

#define HX_HASH_INDEX_LEN 8

#define HXTimeIDToResponseKeyPack(val, time, id) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX*3); \
	kvs_bind_uint64((val), HXTimeIDToResponse); \
	kvs_bind_uint64((val), (time)); \
	kvs_bind_uint64((val), (id)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define HXTimeIDToResponseRange0(range) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX); \
	kvs_bind_uint64((range)->min, HXTimeIDToResponse); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void HXTimeIDToResponseKeyUnpack(KVS_val *const val, uint64_t *const time, uint64_t *const id) {
	uint64_t const table = kvs_read_uint64(val);
	assert(HXTimeIDToResponse == table);
	*time = kvs_read_uint64(val);
	*id = kvs_read_uint64(val);
}
static void HXTimeIDToResponseValUnpack(KVS_val *const val, KVS_txn *const txn, struct response *const out) {
	assert(out);
	strarg_t const url = kvs_read_string(val, txn);
	int const status = kvs_read_uint64(val) - 0xffff;
	strarg_t const type = kvs_read_string(val, txn);
	uint64_t const length = kvs_read_uint64(val);
	strlcpy(out->url, url, sizeof(out->url));
	out->status = status;
	strlcpy(out->type, type, sizeof(out->type));
	assert(UINT64_MAX == (uint64_t)-1);
	out->length = length-1; // 0 -> UINT64_MAX
	for(size_t i = 0; i < HASH_ALGO_MAX; i++) {
		if(0 == val->size) {
			out->digests[i].len = 0;
			continue;
		}
		uint64_t const len = kvs_read_uint64(val);
		kvs_assert(len <= hash_algo_digest_len(i));
		unsigned char const *const buf = kvs_read_blob(val, len);
		out->digests[i].len = len;
		memcpy(out->digests[i].buf, buf, len);
	}
}

#define HXURLSurtAndTimeIDKeyPack(val, txn, url, time, id) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX*3 + KVS_INLINE_MAX); \
	kvs_bind_uint64((val), HXURLSurtAndTimeID); \
	kvs_bind_string((val), (url), (txn)); \
	kvs_bind_uint64((val), (time)); \
	kvs_bind_uint64((val), (id)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define HXURLSurtAndTimeIDRange1(range, txn, url) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX+KVS_INLINE_MAX); \
	kvs_bind_uint64((range)->min, HXURLSurtAndTimeID); \
	kvs_bind_string((range)->min, (url), (txn)); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void HXURLSurtAndTimeIDKeyUnpack(KVS_val *const val, KVS_txn *const txn, strarg_t *const url, uint64_t *const time, uint64_t *const id) {
	uint64_t const table = kvs_read_uint64(val);
	assert(HXURLSurtAndTimeID == table);
	*url = kvs_read_string(val, txn);
	*time = kvs_read_uint64(val);
	*id = kvs_read_uint64(val);
}

#define HXTimeIDQueuedURLAndClientKeyPack(val, txn, time, id, url, client) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX*3 + KVS_INLINE_MAX*2) \
	kvs_bind_uint64((val), HXTimeIDQueuedURLAndClient); \
	kvs_bind_uint64((val), (time)); \
	kvs_bind_uint64((val), (id)); \
	kvs_bind_string((val), (url), (txn)); \
	kvs_bind_string((val), (client), (txn)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define HXTimeIDQueuedURLAndClientRange0(range) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX) \
	kvs_bind_uint64((range)->min, HXTimeIDQueuedURLAndClient); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void HXTimeIDQueuedURLAndClientKeyUnpack(KVS_val *const val, KVS_txn *const txn, uint64_t *const time, uint64_t *const id, strarg_t *const URL, strarg_t *const client) {
	uint64_t const table = kvs_read_uint64(val);
	assert(HXTimeIDQueuedURLAndClient == table);
	*time = kvs_read_uint64(val);
	*id = kvs_read_uint64(val);
	*URL = kvs_read_string(val, txn);
	*client = kvs_read_string(val, txn);
}

#define HXQueuedURLSurtAndTimeIDKeyPack(val, txn, url, time, id) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX*3 + KVS_INLINE_MAX); \
	kvs_bind_uint64((val), HXQueuedURLSurtAndTimeID); \
	kvs_bind_string((val), (url), (txn)); \
	kvs_bind_uint64((val), (time)); \
	kvs_bind_uint64((val), (id)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define HXQueuedURLSurtAndTimeIDRange1(range, txn, url) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX+KVS_INLINE_MAX); \
	kvs_bind_uint64((range)->min, HXQueuedURLSurtAndTimeID); \
	kvs_bind_string((range)->min, (url), (txn)); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void HXQueuedURLSurtAndTimeIDKeyUnpack(KVS_val *const val, KVS_txn *const txn, strarg_t *const url, uint64_t *const time, uint64_t *const id) {
	uint64_t const table = kvs_read_uint64(val);
	assert(HXQueuedURLSurtAndTimeID == table);
	*url = kvs_read_string(val, txn);
	*time = kvs_read_uint64(val);
	*id = kvs_read_uint64(val);
}

#define HXAlgoHashAndTimeIDKeyPack(val, algo, hash, time, id) \
	KVS_VAL_STORAGE(val, KVS_VARINT_MAX*3 + KVS_BLOB_MAX(HX_HASH_INDEX_LEN)); \
	kvs_bind_uint64((val), HXHashAndTimeID+(algo)); \
	kvs_bind_blob((val), (hash), HX_HASH_INDEX_LEN); \
	kvs_bind_uint64((val), (time)); \
	kvs_bind_uint64((val), (id)); \
	KVS_VAL_STORAGE_VERIFY(val);
#define HXAlgoHashAndTimeIDRange2(range, algo, hash, len) \
	KVS_RANGE_STORAGE(range, KVS_VARINT_MAX + KVS_BLOB_MAX(HX_HASH_INDEX_LEN)); \
	kvs_bind_uint64((range)->min, HXHashAndTimeID+(algo)); \
	kvs_bind_blob((range)->min, (hash), MIN(HX_HASH_INDEX_LEN, (len))); \
	kvs_range_genmax((range)); \
	KVS_RANGE_STORAGE_VERIFY(range);
static void HXAlgoHashAndTimeIDKeyUnpack(KVS_val *const val, hash_algo *const algo, unsigned char const **const hash, uint64_t *const time, uint64_t *const id) {
	uint64_t const table = kvs_read_uint64(val);
	assert(table >= HXHashAndTimeID);
	assert(table < HXHashAndTimeID+HASH_ALGO_MAX);
	*algo = table - HXHashAndTimeID;
	*hash = kvs_read_blob(val, HX_HASH_INDEX_LEN);
	*time = kvs_read_uint64(val);
	*id = kvs_read_uint64(val);
}

