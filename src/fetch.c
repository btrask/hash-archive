// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <async/http/HTTP.h>
#include "util/hash.h"
#include "util/url.h"
#include "db.h"
#include "page.h" // TODO: Only for general defs

#define USER_AGENT "Hash Archive (https://github.com/btrask/hash-archive)"

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
int url_fetch(strarg_t const URL, strarg_t const client, struct response *const out) {
	assert(out);
	if(!URL) return UV_EINVAL;

	HTTPConnectionRef conn = NULL;
	HTTPHeadersRef headers = NULL;
	uint64_t length = 0;
	hasher_t *hasher = NULL;
	char const *type = NULL;
	int rc = 0;

	// Pre-initialize all fields, because our errors are non-fatal.
	out->time = time(NULL);
	strlcpy(out->url, URL, sizeof(out->url));
	out->status = 0;
	strlcpy(out->type, "", sizeof(out->type));
	out->length = 0;
	for(size_t i = 0; i < numberof(out->digests); i++) {
		out->digests[i].len = 0;
	}

	rc = rc < 0 ? rc : send_get(URL, client, &conn);
	rc = rc < 0 ? rc : HTTPConnectionReadResponseStatus(conn, &out->status);
	rc = rc < 0 ? rc : HTTPHeadersCreateFromConnection(conn, &headers);
	if(rc < 0) goto cleanup;

	type = HTTPHeadersGet(headers, "Content-Type");
	if(type) strlcpy(out->type, type, sizeof(out->type));

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
	out->length = length;
	rc = hasher_digests(hasher, out->digests, numberof(out->digests));
	if(rc < 0) goto cleanup;

cleanup:
	HTTPConnectionFree(&conn);
	HTTPHeadersFree(&headers);
	hasher_free(&hasher);
	type = NULL;
	if(rc < 0) {
		out->status = rc;
		return 0;
	}
	return 0;
}

