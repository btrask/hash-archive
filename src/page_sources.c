// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <stdlib.h>
#include <string.h>
#include "util/strext.h"
#include "page.h"
#include "db.h"
#include "errors.h"
#include "common.h"
#include "config.h"

static TemplateRef header = NULL;
static TemplateRef footer = NULL;
static TemplateRef entry = NULL;
static TemplateRef notfound = NULL;
static TemplateRef short_hash = NULL;
static TemplateRef weak_hash = NULL;

static int source_var(void *const actx, char const *const var, TemplateWriteFn const wr, void *const wctx) {
	struct response const *const res = actx;

	if(0 == strcmp(var, "date")) {
		char *date = res->flags & HX_RES_LATEST ?
			date_html("Active as of ", res->time) :
			date_html("Obsolete; last seen ", res->time);
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
	if(0 == strcmp(var, "obsolete")) {
		if(res->flags & HX_RES_LATEST) return 0;
		return wr(wctx, uv_buf_init((char *)STR_LEN("obsolete")));
	}

	return UV_ENOENT;
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

	struct response *responses = NULL;
	char *escaped = NULL;
	char *hash_link = NULL;
	char *google_url = NULL;
	char *ddg_url = NULL;
	char *ipfs_url = NULL;
	char *virustotal_url = NULL;
	int rc = 0;

	hash_uri_t obj[1];
	rc = hash_uri_parse(URI, obj);
	if(rc < 0) goto cleanup;

	responses = calloc(CONFIG_SOURCES_MAX, sizeof(struct response));
	if(!responses) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	ssize_t const count = hx_get_sources(obj, responses, CONFIG_SOURCES_MAX);
	if(count < 0) rc = count;
	if(rc < 0) goto cleanup;

	// These don't need escaping because they are restricted character sets.
	char hex[HASH_DIGEST_MAX*2+1];
	rc = hex_encode(obj->buf, obj->len, hex, sizeof(hex));
	if(rc < 0) goto cleanup;
	char multihash[URI_MAX];
	rc = hash_uri_variant(obj, LINK_MULTIHASH, multihash, sizeof(multihash));
	if(HASH_ENOTSUP == rc) {
		strlcpy(multihash, "", sizeof(multihash));
		rc = 0;
	}
	if(rc < 0) goto cleanup;

	escaped = html_encode(URI);
	hash_link = direct_link_html(obj->type, URI);
	google_url = aasprintf("https://www.google.com/search?q=%s", hex);
	ddg_url = aasprintf("https://duckduckgo.com/?q=%s", hex);
	ipfs_url = multihash[0] ?
		aasprintf("https://ipfs.io/api/v0/block/get?arg=%s", multihash) :
		aasprintf("#unsupported-input"); // TODO: Better error handling here?
	virustotal_url = aasprintf("https://www.virustotal.com/en/file/%s/analysis/", hex);

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

	// TODO: Show weak and/or short hash warnings here!

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
	return rc;
}

