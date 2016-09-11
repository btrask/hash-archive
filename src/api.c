// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <string.h>
#include <async/http/status.h>
#include <yajl/yajl_gen.h>
#include "util/hash.h"
#include "util/url.h"
#include "page.h"
#include "db.h"
#include "common.h"
#include "errors.h"
#include "config.h"

static yajl_gen_status yajl_gen_string2(yajl_gen hand, const char * str, size_t len) {
	return yajl_gen_string(hand, (unsigned char const *)str, len);
}
static void yajl_print_cb(void *ctx, const char *str, size_t len) {
	HTTPConnectionRef const conn = ctx;
	(void) HTTPConnectionWriteChunk(conn, (unsigned char const *)str, len);
}

static void res_json(struct response const *const res, yajl_gen const json) {
	yajl_gen_map_open(json);
	yajl_gen_string2(json, STR_LEN("url"));
	yajl_gen_string2(json, res->url, strlen(res->url));
	yajl_gen_string2(json, STR_LEN("timestamp"));
	yajl_gen_integer(json, 1000*res->time);
	yajl_gen_string2(json, STR_LEN("status"));
	yajl_gen_integer(json, res->status);
	yajl_gen_string2(json, STR_LEN("type"));
	yajl_gen_string2(json, res->type, strlen(res->type));
	yajl_gen_string2(json, STR_LEN("length"));
	yajl_gen_integer(json, res->status);
//	yajl_gen_string2(json, STR_LEN("latest"));
//	yajl_gen_bool(json, !!(res->flags & HX_RES_LATEST));
	yajl_gen_string2(json, STR_LEN("hashes"));
	yajl_gen_array_open(json);
	for(size_t i = 0; i < HASH_ALGO_MAX; i++) {
		hash_uri_t obj = {
			.type = LINK_HASH_URI,
			.algo = i,
			.buf = (unsigned char *)res->digests[i].buf,
			.len = res->digests[i].len,
		};
		if(!obj.len) continue;
		char hash[URI_MAX];
		hash_uri_format(&obj, hash, sizeof(hash));
		yajl_gen_string2(json, hash, strlen(hash));
	}
	yajl_gen_array_close(json);
	yajl_gen_map_close(json);
}
static int response_list(HTTPConnectionRef const conn, struct response const *const responses, size_t const count) {
	yajl_gen json = NULL;
	int rc = 0;

	json = yajl_gen_alloc(NULL);
	if(!json) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;
	yajl_gen_config(json, yajl_gen_print_callback, yajl_print_cb, conn);
	yajl_gen_config(json, yajl_gen_beautify, 1);

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	yajl_gen_array_open(json);

	for(size_t i = 0; i < count; i++) {
		res_json(&responses[i], json);
	}

	yajl_gen_array_close(json);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);
cleanup:
	if(json) yajl_gen_free(json);
	json = NULL;
	return rc;
}

int api_enqueue(HTTPConnectionRef const conn, strarg_t const URL) {
	return UV_ENOSYS; // TODO
}
int api_history(HTTPConnectionRef const conn, strarg_t const URL) {
	struct response *responses = NULL;
	int rc = 0;

	url_t obj[1];
	rc = url_parse(URL, obj);
	if(rc < 0) goto cleanup;

	responses = calloc(CONFIG_API_HISTORY_MAX, sizeof(struct response));
	if(!responses) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	ssize_t const count = hx_get_history(URL, responses, CONFIG_API_HISTORY_MAX);
	if(count < 0) rc = count;
	if(rc < 0) goto cleanup;

	rc = response_list(conn, responses, count);
	if(rc < 0) goto cleanup;

cleanup:
	FREE(&responses);
	return rc;
}
int api_sources(HTTPConnectionRef const conn, strarg_t const hash) {
	struct response *responses = NULL;
	int rc = 0;

	hash_uri_t obj[1];
	rc = hash_uri_parse(hash, obj);
	if(rc < 0) goto cleanup;

	responses = calloc(CONFIG_API_SOURCES_MAX, sizeof(struct response));
	if(!responses) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	ssize_t const count = hx_get_sources(obj, responses, CONFIG_API_SOURCES_MAX);
	if(count < 0) rc = count;
	if(rc < 0) goto cleanup;

	rc = response_list(conn, responses, count);
	if(rc < 0) goto cleanup;

cleanup:
	FREE(&responses);
	return rc;
}

