// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

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

int page_history(HTTPConnectionRef const conn, strarg_t const URL) {
	int rc = 0;
	if(!header) {
		template_load("history-header.html", &header);
		template_load("history-footer.html", &footer);
		template_load("history-entry.html", &entry);
		template_load("history-error.html", &error);
		template_load("history-outdated.html", &outdated);
	}

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
	for(; rc >= 0; rc = db_cursor_nextr(cursor, range, key, NULL, -1)) {
		strarg_t url;
		uint64_t time, id;
		HXURLSurtAndTimeIDKeyUnpack(key, txn, &url, &time, &id);
		fprintf(stderr, "%s, %llu, %llu\n", url, (unsigned long long)time, (unsigned long long)id);
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
	TemplateWriteHTTPChunk(header, &TemplateStaticCBs, &args, conn);
	TemplateWriteHTTPChunk(footer, &TemplateStaticCBs, &args, conn);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);

	return 0;
}

