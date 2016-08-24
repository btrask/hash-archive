// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <string.h>
#include "util/hash.h"
#include "util/html.h"
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

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(header, NULL, NULL, conn);

	for(size_t i = 0; i < count; i++) {
		TemplateWriteHTTPChunk(entry, source_var, &responses[i], conn);
	}

	TemplateWriteHTTPChunk(footer, NULL, NULL, conn);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);

cleanup:
	FREE(&responses);
	return 0;
}

