// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include "util/hash.h"
#include "util/html.h"
#include "page.h"
#include "db.h"
#include "common.h"

static TemplateRef header = NULL;
static TemplateRef footer = NULL;
static TemplateRef entry = NULL;
static TemplateRef notfound = NULL;
static TemplateRef short_hash = NULL;
static TemplateRef weak_hash = NULL;

int page_sources(HTTPConnectionRef const conn, strarg_t const URI) {
	if(!header) {
		template_load("sources-header.html", &header);
		template_load("sources-footer.html", &footer);
		template_load("sources-entry.html", &entry);
		template_load("sources-notfound.html", &notfound);
		template_load("sources-short.html", &short_hash);
		template_load("sources-weak.html", &weak_hash);
	}
	int rc = 0;

	hash_uri_t obj[1];
	rc = hash_uri_parse(URI, obj);
	if(rc < 0) return rc;


{

	DB_env *db = NULL;
	DB_txn *txn = NULL;
	DB_cursor *cursor = NULL;

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
	for(; rc >= 0; rc = db_cursor_nextr(cursor, range, hash_key, NULL, -1)) {
		hash_algo algo;
		unsigned char const *hash;
		uint64_t time, id;
		HXAlgoHashAndTimeIDKeyUnpack(hash_key, &algo, &hash, &time, &id);

		DB_val res_key[1], res_val[1];
		HXTimeIDToResponseKeyPack(res_key, time, id);
		rc = db_get(txn, res_key, res_val);
		if(rc < 0) goto cleanup;

		strarg_t url, type;
		int status;
		uint64_t length;
		size_t hlens[HASH_ALGO_MAX];
		unsigned char const *hashes[HASH_ALGO_MAX];
		HXTimeIDToResponseValUnpack(res_val, txn, &url, &status, &type, &length, hlens, hashes);

		hash_uri_t obj2[1];
		obj2->type = obj->type;
		obj2->algo = obj->algo;
		obj2->buf = (unsigned char *)hashes[obj->algo];
		obj2->len = hlens[obj->algo];
		char pretty[URI_MAX];
		hash_uri_format(obj2, pretty, sizeof(pretty));
		fprintf(stderr, "test %s = %s\n", URI, pretty);



	}



cleanup:
	db_cursor_close(cursor); cursor = NULL;
	db_txn_abort(txn); txn = NULL;
	hx_db_close(&db);





}




	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(header, NULL, NULL, conn);


	TemplateWriteHTTPChunk(footer, NULL, NULL, conn);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);

	return 0;
}

