// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <kvstore/db_schema.h>

enum {
	// TODO: Old name format
	harc_timeid_to_entry = 20,
	harc_url_timeid = 21,
	harc_hash_timeid = 50,

	HXTimeIDQueuedURLAndClient = 30,
};

#define harc_timeid_to_entry_keypack(val, time, id) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX*3); \
	db_bind_uint64((val), harc_timeid_to_entry); \
	db_bind_uint64((val), (time)); \
	db_bind_uint64((val), (id)); \
	DB_VAL_STORAGE_VERIFY(val);

// TODO: How to pack/unpack list of hashes?

#define harc_hash_timeid_keypack(val, url, time, id) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX*3 + DB_INLINE_MAX); \
	db_bind_uint64((val), harc_url_timeid); \
	db_bind_string((val), (url), 8); \
	db_bind_uint64((val), (time)); \
	db_bind_uint64((val), (id)); \
	DB_VAL_STORAGE_VERIFY(val);

/*#define harc_hash_timeid_keypack(val, algo, hash, time, id) \
	DB_VAL_STORAGE(val, DB_VARINT_MAX*3 + DB_BLOB_MAX(8)); \
	db_bind_uint64((val), harc_hash_timeid+(algo)); \
	db_bind_blob((val), (hash), 8); \
	db_bind_uint64((val), (time)); \
	db_bind_uint64((val), (id)); \
	DB_VAL_STORAGE_VERIFY(val);*/

