// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <string.h>
#include "util/strext.h"
#include "page.h"
#include "db.h"
#include "common.h"

#define RESPONSES_MAX 30

static TemplateRef header = NULL;
static TemplateRef footer = NULL;
static TemplateRef entry = NULL;
static TemplateRef notfound = NULL;
static TemplateRef short_hash = NULL;
static TemplateRef weak_hash = NULL;

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

static int source_var(void *const actx, char const *const var, TemplateWriteFn const wr, void *const wctx) {
	struct response const *const res = actx;

	if(0 == strcmp(var, "date")) {
		char *date = date_html("Last seen ", res->time);
		int rc = wr(wctx, uv_buf_init(date, strlen(date)));
		free(date); date = NULL;
		return rc;
	}
	if(0 == strcmp(var, "url")) {
		char *url_safe = link_html(LINK_WEB_URL, res->url);
		int rc = wr(wctx, uv_buf_init(url_safe, strlen(url_safe)));
		free(url_safe); url_safe = NULL;
		return rc;
	}

	return 0;
}

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

	struct response *responses = calloc(RESPONSES_MAX, sizeof(struct response));
	assert(responses);

	ssize_t count = hx_get_sources(obj, responses, RESPONSES_MAX);
	if(count < 0) {
		HTTPConnectionSendStatus(conn, HTTPError(count));
		goto cleanup;
	}

	res_merge_common_urls(responses, count);

	// These don't need escaping because they are restricted character sets.
	char hex[HASH_DIGEST_MAX*2+1];
	rc = hex_encode(obj->buf, obj->len, hex, sizeof(hex));
	assert(rc >= 0);
	char multihash[URI_MAX];
	obj->type = LINK_MULTIHASH;
	rc = hash_uri_format(obj, multihash, sizeof(multihash));
	assert(rc >= 0);

	char *escaped = html_encode(URI);
	char *hash_link = direct_link_html(obj->type, URI);
	char *google_url = aasprintf("https://www.google.com/search?q=%s", hex);
	char *ddg_url = aasprintf("https://duckduckgo.com/?q=%s", hex);
	char *ipfs_url = aasprintf("https://ipfs.io/api/v0/block/get?arg=%s", multihash);
	char *virustotal_url = aasprintf("https://www.virustotal.com/en/file/%s/analysis/", hex);

	TemplateStaticArg args[] = {
		{"query", escaped},
		{"hash-link", hash_link},
		{"google-url", google_url},
		{"duckduckgo-url", ddg_url},
		{"ipfs-block-url", ipfs_url},
		{"virustotal-url", virustotal_url},
		{NULL, NULL},
	};

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(header, TemplateStaticVar, args, conn);

	// TODO: If there are any response hashes that don't exactly match
	// the user's query (i.e. they're longer), then show "search suggestions".

	for(size_t i = 0; i < count; i++) {
		if(responses[i].prev) continue; // Skip duplicates
		TemplateWriteHTTPChunk(entry, source_var, &responses[i], conn);
	}

	TemplateWriteHTTPChunk(footer, TemplateStaticVar, args, conn);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);

cleanup:
	FREE(&responses);
	FREE(&escaped);
	FREE(&hash_link);
	FREE(&google_url);
	FREE(&ddg_url);
	FREE(&ipfs_url);
	FREE(&virustotal_url);
	return 0;
}

