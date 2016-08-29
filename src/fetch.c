// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <async/http/HTTP.h>
#include "util/hash.h"
#include "util/url.h"
#include "db.h"
#include "page.h" // TODO: Only for general defs
#include "errors.h"

#define USER_AGENT "Hash Archive (https://github.com/btrask/hash-archive)"
#define REDIRECT_MAX 5

static int send_get(strarg_t const URL, strarg_t const client, HTTPConnectionRef *const out) {
	assert(out);
	HTTPConnectionRef conn = NULL;
	url_t obj[1];
	int rc = 0;
	rc = url_parse(URL, obj);
	if(rc < 0) goto cleanup;
	rc = rc < 0 ? rc : HTTPConnectionCreateOutgoingSecure(obj->host, 0, NULL, &conn);
	rc = rc < 0 ? rc : HTTPConnectionWriteRequest(conn, HTTP_GET, obj->path, obj->host);
	rc = rc < 0 ? rc : HTTPConnectionWriteHeader(conn, "User-Agent", USER_AGENT);
	rc = rc < 0 ? rc : HTTPConnectionWriteHeader(conn, "X-Forwarded-For", client); // TODO
	HTTPConnectionSetKeepAlive(conn, false); // No point.
	rc = rc < 0 ? rc : HTTPConnectionBeginBody(conn);
	rc = rc < 0 ? rc : HTTPConnectionEnd(conn);
	if(rc < 0) goto cleanup;
	*out = conn; conn = NULL;
cleanup:
	HTTPConnectionFree(&conn);
	return rc;
}
static int url_fetch_internal(struct response *const res, strarg_t const client) {
	assert(res);

	HTTPConnectionRef conn = NULL;
	HTTPHeadersRef headers = NULL;
	uint64_t length = 0;
	hasher_t *hasher = NULL;
	char const *type = NULL;
	int rc = 0;

	rc = rc < 0 ? rc : send_get(res->url, client, &conn);
	rc = rc < 0 ? rc : HTTPConnectionReadResponseStatus(conn, &res->status);
	rc = rc < 0 ? rc : HTTPHeadersCreateFromConnection(conn, &headers);
	if(rc < 0) goto cleanup;

	if(res->status >= 300 && res->status < 400) {
		char const *const loc = HTTPHeadersGet(headers, "Location");
		if(loc) {
			strlcpy(res->url, loc, sizeof(res->url));
			rc = HX_ERR_REDIRECT;
			goto cleanup;
		}
	}

	type = HTTPHeadersGet(headers, "Content-Type");
	if(type) strlcpy(res->type, type, sizeof(res->type));

	rc = hasher_create(HASHER_ALGOS_ALL, &hasher);
	if(rc < 0) goto cleanup;
	for(;;) {
		uv_buf_t buf[1];
		rc = HTTPConnectionReadBody(conn, buf);
		if(rc < 0) goto cleanup;
		if(0 == buf->len) break;
		async_pool_enter(NULL);
		rc = hasher_update(hasher, (unsigned char *)buf->base, buf->len);
		async_pool_leave(NULL);
		if(rc < 0) goto cleanup;
		length += buf->len;
	}
	res->length = length;
	rc = hasher_digests(hasher, res->digests, numberof(res->digests));
	if(rc < 0) goto cleanup;

cleanup:
	HTTPConnectionFree(&conn);
	HTTPHeadersFree(&headers);
	hasher_free(&hasher);
	type = NULL;
	if(rc < 0) {
		res->status = rc;
		return 0;
	}
	return 0;
}
int url_fetch(strarg_t const URL, strarg_t const client, struct response *const out) {
	assert(out);
	if(!URL) return UV_EINVAL;

	// Pre-initialize all fields, because our errors are non-fatal.
	out->time = time(NULL);
	strlcpy(out->url, URL, sizeof(out->url));
	out->status = 0;
	strlcpy(out->type, "", sizeof(out->type));
	out->length = 0;
	for(size_t i = 0; i < numberof(out->digests); i++) {
		out->digests[i].len = 0;
	}

	for(size_t i = 0; i < REDIRECT_MAX; i++) {
		int rc = url_fetch_internal(out, client);
		if(rc < 0) return rc;
		if(HX_ERR_REDIRECT != out->status) return rc;
	}
	return 0;
}

