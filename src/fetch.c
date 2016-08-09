// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <async/http/HTTP.h>
#include "util/hash.h"
#include "util/url.h"
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
int url_fetch(strarg_t const URL, strarg_t const client, int *const outstatus, HTTPHeadersRef *const outheaders, uint64_t *const outlength, hasher_t **const outhasher) {
	assert(outstatus);
	assert(outheaders);
	assert(outlength);
	assert(outhasher);

	HTTPConnectionRef conn = NULL;
	HTTPHeadersRef headers = NULL;
	uint64_t length = 0;
	hasher_t *hasher = NULL;
	int status = 0;
	int rc = 0;

	rc = rc < 0 ? rc : send_get(URL, client, &conn);
	rc = rc < 0 ? rc : HTTPConnectionReadResponseStatus(conn, &status);
	rc = rc < 0 ? rc : HTTPHeadersCreateFromConnection(conn, &headers);
	if(rc < 0) goto cleanup;

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
	rc = hasher_finish(hasher);
	if(rc < 0) goto cleanup;

	*outstatus = status; status = 0;
	*outheaders = headers; headers = NULL;
	*outlength = length; length = 0;
	*outhasher = hasher; hasher = NULL;

cleanup:
	HTTPConnectionFree(&conn);
	HTTPHeadersFree(&headers);
	hasher_free(&hasher);
	return rc;
}

