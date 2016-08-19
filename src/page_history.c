// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <string.h>
#include "util/hash.h"
#include "util/html.h"
#include "util/url.h"
#include "page.h"
#include "db.h"

static TemplateRef header = NULL;
static TemplateRef footer = NULL;
static TemplateRef entry = NULL;
static TemplateRef error = NULL;
static TemplateRef outdated = NULL;

typedef struct {
	uint64_t time;
	int status;
	char type[255+1];
	uint64_t length;
	size_t hlen[HASH_ALGO_MAX];
	unsigned char hashes[HASH_ALGO_MAX][HASH_DIGEST_MAX];
} response_t;


int page_history(HTTPConnectionRef const conn, strarg_t const URL) {
	int rc = 0;
	if(!header) {
		template_load("history-header.html", &header);
		template_load("history-footer.html", &footer);
		template_load("history-entry.html", &entry);
		template_load("history-error.html", &error);
		template_load("history-outdated.html", &outdated);
	}

	response_t responses[30][1] = {};
	size_t i = 0;

{
	char surt[URI_MAX];
	rc = url_normalize_surt(URL, surt, sizeof(surt));

	DB_env *db = NULL;
	DB_txn *txn = NULL;
	DB_cursor *cursor = NULL;

	rc = hx_db_open(&db);
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	rc = db_cursor_open(txn, &cursor);

	DB_range range[1];
	DB_val key[1];
	HXURLSurtAndTimeIDRange1(range, txn, surt);
	rc = db_cursor_firstr(cursor, range, key, NULL, -1);
	for(; rc >= 0 && i < numberof(responses); i++, rc = db_cursor_nextr(cursor, range, key, NULL, -1)) {
		strarg_t surt;
		uint64_t time, id;
		HXURLSurtAndTimeIDKeyUnpack(key, txn, &surt, &time, &id);

		DB_val res_key[1], res_val[1];
		HXTimeIDToResponseKeyPack(res_key, time, id);
		rc = db_get(txn, res_key, res_val);
		strarg_t const url = db_read_string(res_val, txn);
		int const status = db_read_uint64(res_val) - 0xffff;
		strarg_t const type = db_read_string(res_val, txn);
		uint64_t const length = db_read_uint64(res_val);

		responses[i]->time = time;
		responses[i]->status = status;
		strlcpy(responses[i]->type, type, sizeof(responses[i]->type));
		responses[i]->length = length;
		for(size_t j = 0; j < HASH_ALGO_MAX; j++) {
			size_t x = db_read_blob(res_val, responses[i]->hashes[j], HASH_DIGEST_MAX);
			responses[i]->hlen[j] = x;
		}
	}

	db_cursor_close(cursor); cursor = NULL;
	db_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
}


	TemplateStaticArg args[] = {
		{NULL, NULL},
	};
	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(header, TemplateStaticVar, &args, conn);


	for(size_t j = 0; j < i; j++) {
		TemplateWriteHTTPChunk(entry, TemplateStaticVar, &args, conn);
		fprintf(stderr, "%llu\n", (unsigned long long)responses[j]->time);
	}


	TemplateWriteHTTPChunk(footer, TemplateStaticVar, &args, conn);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);

	return 0;
}

