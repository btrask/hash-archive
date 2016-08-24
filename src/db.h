// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <kvstore/db_schema.h>

// TODO
typedef char const *strarg_t;

int hx_db_load(void);
int hx_db_open(DB_env **const out);
void hx_db_close(DB_env **const in);

char const *hx_strerror(int const rc);

enum {
	// 0-19 reserved.
	// Remember this is the permanent on-disk format.

	HXTimeIDToResponse = 20,
	HXURLSurtAndTimeID = 21,

	HXTimeIDQueuedURLAndClient = 30,

	HXHashAndTimeID = 50, // Note: hashes truncated, not necessarily unique!
};

#define HX_HASH_INDEX_LEN 8

#define HXTimeIDToResponseKeyPack(val, time, id) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX*3); \
	db_bind_uint64((val), HXTimeIDToResponse); \
	db_bind_uint64((val), (time)); \
	db_bind_uint64((val), (id)); \
	DB_VAL_STORAGE_VERIFY(val);
#define HXTimeIDToResponseRange0(range) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX); \
	db_bind_uint64((range)->min, HXTimeIDToResponse); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);

#define HXURLSurtAndTimeIDKeyPack(val, txn, url, time, id) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX*3 + DB_INLINE_MAX); \
	db_bind_uint64((val), HXURLSurtAndTimeID); \
	db_bind_string((val), (url), (txn)); \
	db_bind_uint64((val), (time)); \
	db_bind_uint64((val), (id)); \
	DB_VAL_STORAGE_VERIFY(val);
#define HXURLSurtAndTimeIDRange1(range, txn, url) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX+DB_INLINE_MAX); \
	db_bind_uint64((range)->min, HXURLSurtAndTimeID); \
	db_bind_string((range)->min, (url), (txn)); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void HXURLSurtAndTimeIDKeyUnpack(DB_val *const val, DB_txn *const txn, strarg_t *const url, uint64_t *const time, uint64_t *const id) {
	uint64_t const table = db_read_uint64(val);
	assert(HXURLSurtAndTimeID == table);
	*url = db_read_string(val, txn);
	*time = db_read_uint64(val);
	*id = db_read_uint64(val);
}

#define HXTimeIDQueuedURLAndClientKeyPack(val, txn, time, id, url, client) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX*3 + DB_INLINE_MAX*2) \
	db_bind_uint64((val), HXTimeIDQueuedURLAndClient); \
	db_bind_uint64((val), (time)); \
	db_bind_uint64((val), (id)); \
	db_bind_string((val), (url), (txn)); \
	db_bind_string((val), (client), (txn)); \
	DB_VAL_STORAGE_VERIFY(val);
#define HXTimeIDQueuedURLAndClientRange0(range) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX) \
	db_bind_uint64((range)->min, HXTimeIDQueuedURLAndClient); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);
static void HXTimeIDQueuedURLAndClientKeyUnpack(DB_val *const val, DB_txn *const txn, uint64_t *const time, uint64_t *const id, strarg_t *const URL, strarg_t *const client) {
	uint64_t const table = db_read_uint64(val);
	assert(HXTimeIDQueuedURLAndClient == table);
	*time = db_read_uint64(val);
	*id = db_read_uint64(val);
	*URL = db_read_string(val, txn);
	*client = db_read_string(val, txn);
}

#define HXAlgoHashAndTimeIDKeyPack(val, algo, hash, time, id) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX*3 + DB_BLOB_MAX(HX_HASH_INDEX_LEN)); \
	db_bind_uint64((val), HXHashAndTimeID+(algo)); \
	db_bind_blob((val), (hash), HX_HASH_INDEX_LEN); \
	db_bind_uint64((val), (time)); \
	db_bind_uint64((val), (id)); \
	DB_VAL_STORAGE_VERIFY(val);
#define HXAlgoHashAndTimeIDRange2(range, algo, hash, len) \
	DB_RANGE_STORAGE(range, DB_VARINT_MAX + DB_BLOB_MAX(HX_HASH_INDEX_LEN)); \
	db_bind_uint64((range)->min, HXHashAndTimeID+(algo)); \
	db_bind_blob((range)->min, (hash), MIN(HX_HASH_INDEX_LEN, (len))); \
	db_range_genmax((range)); \
	DB_RANGE_STORAGE_VERIFY(range);


