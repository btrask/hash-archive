// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <string.h>
#include "util/hash.h"
#include "util/html.h"
#include "util/url.h"
#include "page.h"
#include "db.h"

// TODO: Get rid of this duplication...
#define MIN(a, b) ({ \
	__typeof__(a) const __a = (a); \
	__typeof__(b) const __b = (b); \
	__a < __b ? __a : __b; \
})
#define MAX(a, b) ({ \
	__typeof__(a) const __a = (a); \
	__typeof__(b) const __b = (b); \
	__a > __b ? __a : __b; \
})
#define STR_LEN(x) (x), (sizeof(x)-1)

static TemplateRef header = NULL;
static TemplateRef footer = NULL;
static TemplateRef entry = NULL;
static TemplateRef error = NULL;
static TemplateRef outdated = NULL;

size_t const algos[] = {
	HASH_ALGO_SHA256,
	HASH_ALGO_SHA384,
	HASH_ALGO_SHA512,
	HASH_ALGO_SHA1,
//	HASH_ALGO_MD5,
};
bool const deprecated[HASH_ALGO_MAX] = {
	[HASH_ALGO_SHA1] = true,
//	[HASH_ALGO_MD5] = true,
};


struct response {
	uint64_t time;
	int status;
	char type[255+1];
	uint64_t length;
	size_t hlen[HASH_ALGO_MAX];
	unsigned char hashes[HASH_ALGO_MAX][HASH_DIGEST_MAX];
	struct response *next;
	struct response *prev;
};

static bool res_eq(struct response const *const a, struct response const *const b) {
	if(a == b) return true;
	if(!a || !b) return false;

	// Don't merge responses that aren't "OK".
	if(200 != a->status || 200 != b->status) return false;

	// It's important that types match.
	if(0 != strcmp(a->type, b->type)) return false;

	// Why not?
	if(a->length != b->length) return false;

	// We only compare the prefix. For empty hashes this is zero which is good.
	for(size_t i = 0; i < HASH_ALGO_MAX; i++) {
		size_t const len = MIN(a->hlen[i], b->hlen[i]);
		if(0 != memcmp(a->hashes[i], b->hashes[i], len)) return false;
	}
	return true;
}
static void res_merge_list(struct response *const responses, size_t const len) {
	for(size_t i = 1; i < len; i++) {
		bool const eq = res_eq(&responses[i-1], &responses[i]);
		if(!eq) continue;
		responses[i-1].next = &responses[i];
		responses[i].prev = &responses[i-1];
	}
}
static ssize_t get_responses(strarg_t const URL, struct response *const out, size_t const max) {
	char surt[URI_MAX];
	int rc = url_normalize_surt(URL, surt, sizeof(surt));
	if(rc < 0) return rc;

	DB_env *db = NULL;
	DB_txn *txn = NULL;
	DB_cursor *cursor = NULL;

	rc = hx_db_open(&db);
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	rc = db_cursor_open(txn, &cursor);

	DB_range range[1];
	DB_val key[1];
	HXURLSurtAndTimeIDRange1(range, txn, surt);
	int i = 0;
	rc = db_cursor_firstr(cursor, range, key, NULL, -1);
	for(; rc >= 0 && i < max; i++, rc = db_cursor_nextr(cursor, range, key, NULL, -1)) {
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

		out[i].time = time;
		out[i].status = status;
		strlcpy(out[i].type, type, sizeof(out[i].type));
		out[i].length = length;
		for(size_t j = 0; j < HASH_ALGO_MAX; j++) {
			size_t x = db_read_blob(res_val, out[i].hashes[j], HASH_DIGEST_MAX);
			out[i].hlen[j] = x;
		}
	}

	db_cursor_close(cursor); cursor = NULL;
	db_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	return i;
}


static char *item_html_obj(hash_uri_t const *const obj) {
	char uri[URI_MAX];
	int rc = hash_uri_format(obj, uri, sizeof(uri));
	if(rc < 0) return NULL;
	return item_html(obj->type, "", uri, deprecated[obj->algo]);
}
static int hist_var(void *const actx, char const *const var, TemplateWriteFn const wr, void *const wctx) {
	struct response const *const res = actx;

	if(0 == strcmp(var, "date")) {
		char *date = date_html("As of ", res->time);
		int rc = wr(wctx, uv_buf_init(date, strlen(date)));
		free(date); date = NULL;
		return rc;
	}
	if(0 == strcmp(var, "dates")) {
		struct response const *r = res->next;
		for(; r; r = r->next) {
			char *date = date_html("Also seen ", r->time);
			int rc = wr(wctx, uv_buf_init(date, strlen(date)));
			free(date); date = NULL;
			if(rc < 0) return rc;
		}
		return 0;
	}
	if(0 == strcmp(var, "error")) {
		char x[31+1];
		snprintf(x, sizeof(x), "%d", res->status);
		// TODO: Print human-readable descriptions...
		return wr(wctx, uv_buf_init(x, strlen(x)));
	}

	hash_uri_type type = LINK_NONE;
	if(0 == strcmp(var, "hash-uri-list")) type = LINK_HASH_URI;
	if(0 == strcmp(var, "named-info-list")) type = LINK_NAMED_INFO;
	if(0 == strcmp(var, "multihash-list")) type = LINK_MULTIHASH;
	if(0 == strcmp(var, "prefix-list")) type = LINK_PREFIX;
	if(0 == strcmp(var, "ssb-list")) type = LINK_SSB;
	if(0 == strcmp(var, "magnet-list")) type = LINK_MAGNET;
	if(LINK_NONE == type) return 0;

	for(size_t i = 0; i < numberof(algos); i++) {
		size_t const algo = algos[i];
		if(!hash_algo_names[algo]) continue;
		hash_uri_t const obj[1] = {{
			.type = type,
			.algo = algo,
			.buf = (unsigned char *)res->hashes[algo],
			.len = res->hlen[algo],
		}};
		if(obj->len <= 0) continue;
		char *x = item_html_obj(obj);
		if(!x) return UV_ENOMEM;
		int rc = wr(wctx, uv_buf_init(x, strlen(x)));
		free(x); x = NULL;
		if(rc < 0) return rc;
	}
	return 0;
}

int page_history(HTTPConnectionRef const conn, strarg_t const URL) {
	int rc = 0;
	if(!header) {
		template_load("history-header.html", &header);
		template_load("history-footer.html", &footer);
		template_load("history-entry.html", &entry);
		template_load("history-error.html", &error);
		template_load("history-outdated.html", &outdated);
	}

	struct response responses[30] = {};
	ssize_t count = get_responses(URL, responses, numberof(responses));
	if(count < 0) return rc;

	res_merge_list(responses, numberof(responses));

	TemplateStaticArg args[] = {
		{"url-link", NULL},
		{"wayback-url", NULL},
		{"google-url", NULL},
		{"virustotal-url", NULL},
		{NULL, NULL},
	};
	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(header, TemplateStaticVar, &args, conn);

	for(size_t i = 0; i < count; i++) {
		if(responses[i].prev) continue; // Skip duplicates
		if(200 == responses[i].status) {
			TemplateWriteHTTPChunk(entry, hist_var, &responses[i], conn);
		} else {
			TemplateWriteHTTPChunk(error, hist_var, &responses[i], conn);
		}
	}

	TemplateWriteHTTPChunk(footer, TemplateStaticVar, &args, conn);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);

	return 0;
}

