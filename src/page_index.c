// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <stdlib.h>
#include <string.h>
#include "db.h"
#include "page.h"
#include "config.h"

static TemplateRef index = NULL;

static char example_named_info[URI_MAX] = "";
static char example_prefix[URI_MAX] = "";
static char example_multihash[URI_MAX] = "";
static char example_ssb[URI_MAX] = "";
static char example_magnet[URI_MAX] = "";

static int write_link(TemplateWriteFn const wr, void *const wctx, hash_uri_type const type, strarg_t const URI_unsafe) {
	char *x = link_html(type, URI_unsafe);
	if(!x) return UV_ENOMEM;
	int rc = wr(wctx, uv_buf_init(x, strlen(x)));
	free(x); x = NULL;
	return rc;
}
static int template_var(void *const actx, char const *const var, TemplateWriteFn const wr, void *const wctx) {
	if(0 == strcmp(var, "web-url-example")) {
		return write_link(wr, wctx, LINK_WEB_URL, example_url);
	}
	if(0 == strcmp(var, "hash-uri-example")) {
		return write_link(wr, wctx, LINK_HASH_URI, example_hash_uri);
	}
	if(0 == strcmp(var, "named-info-example")) {
		return write_link(wr, wctx, LINK_NAMED_INFO, example_named_info);
	}
	if(0 == strcmp(var, "multihash-example")) {
		return write_link(wr, wctx, LINK_MULTIHASH, example_multihash);
	}
	if(0 == strcmp(var, "prefix-example")) {
		return write_link(wr, wctx, LINK_PREFIX, example_prefix);
	}
	if(0 == strcmp(var, "ssb-example")) {
		return write_link(wr, wctx, LINK_SSB, example_ssb);
	}
	if(0 == strcmp(var, "magnet-example")) {
		return write_link(wr, wctx, LINK_MAGNET, example_magnet);
	}

	if(0 == strcmp(var, "recent-list")) {
		struct response recent[10];
		ssize_t const count = hx_get_recent(recent, 10);
		if(count < 0) return 0;
		for(size_t i = 0; i < count; i++) {
			char *x = item_html(LINK_WEB_URL, "", recent[i].url, false);
			if(!x) return UV_ENOMEM;
			int rc = wr(wctx, uv_buf_init(x, strlen(x)));
			free(x); x = NULL;
			if(rc < 0) return rc;
		}
		return 0;
	}

	if(0 == strcmp(var, "critical-list")) {
		for(size_t i = 0; i < numberof(critical); i++) {
			char *x = item_html(LINK_WEB_URL, "", critical[i], false);
			if(!x) return UV_ENOMEM;
			int rc = wr(wctx, uv_buf_init(x, strlen(x)));
			free(x); x = NULL;
			if(rc < 0) return rc;
		}
		return 0;
	}

	if(0 == strcmp(var, "host")) {
		char const *host = CONFIG_HOSTNAME_PUBLIC;
		assert(host);
		return wr(wctx, uv_buf_init((char *)host, strlen(host)));
	}

	return UV_ENOENT;
}

int page_index(HTTPConnectionRef const conn) {
	int rc = 0;
	if(!index) {
		template_load("index.html", &index);

		hash_uri_t obj[1] = {};
		rc = hash_uri_parse(example_hash_uri, obj);
		assert(rc >= 0);
		rc = hash_uri_variant(obj, LINK_NAMED_INFO, example_named_info, URI_MAX);
		assert(rc >= 0);
		rc = hash_uri_variant(obj, LINK_PREFIX, example_prefix, URI_MAX);
		assert(rc >= 0);
		rc = hash_uri_variant(obj, LINK_MULTIHASH, example_multihash, URI_MAX);
		assert(rc >= 0);
		rc = hash_uri_variant(obj, LINK_SSB, example_ssb, URI_MAX);
		assert(rc >= 0);
		rc = hash_uri_variant(obj, LINK_MAGNET, example_magnet, URI_MAX);
		assert(rc >= 0);
		hash_uri_destroy(obj);
	}

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(index, template_var, NULL, conn);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);

	return 0;
}

