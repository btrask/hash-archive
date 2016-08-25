// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <string.h>
#include "util/hash.h"
#include "util/html.h"
#include "util/strext.h"
#include "util/url.h"
#include "page.h"
#include "db.h"
#include "common.h"

// TODO
int queue_add(uint64_t const time, strarg_t const URL, strarg_t const client);

#define RESPONSES_MAX 30
#define CRAWL_DELAY_SECONDS (60*60*24)

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
	if(!header) {
		template_load("history-header.html", &header);
		template_load("history-footer.html", &footer);
		template_load("history-entry.html", &entry);
		template_load("history-error.html", &error);
		template_load("history-outdated.html", &outdated);
	}
	int rc = 0;

	struct response *responses = calloc(RESPONSES_MAX, sizeof(struct response));
	assert(responses); // TODO

	char *escaped = html_encode(URL);
	char *link = direct_link_html(LINK_WEB_URL, URL);
	char *wayback_url = aasprintf("https://web.archive.org/web/*/%s", escaped);
	char *google_url = aasprintf("https://webcache.googleusercontent.com/search?q=cache:%s", escaped);
	char *virustotal_url = aasprintf("https://www.virustotal.com/en/url/%s", escaped);

	ssize_t count = hx_get_history(URL, responses, RESPONSES_MAX);
	if(count < 0) {
		HTTPConnectionSendStatus(conn, HTTPError(count));
		goto cleanup;
	}

	res_merge_list(responses, RESPONSES_MAX);

	TemplateStaticArg args[] = {
		{"url-link", link},
		{"wayback-url", wayback_url},
		{"google-url", google_url},
		{"virustotal-url", virustotal_url},
		{NULL, NULL},
	};
	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(header, TemplateStaticVar, &args, conn);

	uint64_t const now = time(NULL);
	if(count <= 0 || responses[0].time < now - CRAWL_DELAY_SECONDS) {
		TemplateWriteHTTPChunk(outdated, TemplateStaticVar, &args, conn);
		rc = queue_add(now, URL, ""); // TODO: Get client
		if(rc < 0) {
			alogf("queue error: %s\n", uv_strerror(rc));
		}
	}

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

cleanup:
	FREE(&responses);
	FREE(&escaped);
	FREE(&link);
	FREE(&wayback_url);
	FREE(&google_url);
	FREE(&virustotal_url);
	return 0;
}

