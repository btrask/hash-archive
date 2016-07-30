// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <async/async.h>
#include <async/http/HTTPServer.h>
#include "strext.h"
#include "hasher.h"
#include "url.h"

#define SERVER_RAW_ADDR NULL
#define SERVER_RAW_PORT 8000

#define URI_MAX (1023+1)
#define USER_AGENT "Hash Archive (https://github.com/btrask/hash-archive)"

typedef char str_t;
typedef char const *strarg_t;

static HTTPServerRef server_raw = NULL;
static HTTPServerRef server_tls = NULL;

// TODO: Remember that we should use the archive's URL representation in the DB.
// http://com,example,www/ or something like that


static int send_get(strarg_t const URL, HTTPConnectionRef *const out) {
	HTTPConnectionRef conn = NULL;
	url_t obj[1];
	int rc = 0;
	rc = url_parse(URL, obj);
	if(rc < 0) goto cleanup;
	rc = rc < 0 ? rc : HTTPConnectionCreateOutgoingSecure(obj->host, 0, NULL, &conn);
	rc = rc < 0 ? rc : HTTPConnectionWriteRequest(conn, HTTP_GET, obj->path, obj->host);
	rc = rc < 0 ? rc : HTTPConnectionWriteHeader(conn, "User-Agent", USER_AGENT);
	HTTPConnectionSetKeepAlive(conn, false); // No point.
	rc = rc < 0 ? rc : HTTPConnectionBeginBody(conn);
	rc = rc < 0 ? rc : HTTPConnectionFlush(conn);
	if(rc < 0) goto cleanup;
	*out = conn; conn = NULL;
cleanup:
	HTTPConnectionFree(&conn);
	return rc;
}
static int url_fetch(strarg_t const URL, int *const outstatus, HTTPHeadersRef *const outheaders, hasher_t **const outhasher) {
	HTTPConnectionRef conn = NULL;
	HTTPHeadersRef headers = NULL;
	hasher_t *hasher = NULL;
	int status = 0;
	int rc = 0;

	rc = rc < 0 ? rc : send_get(URL, &conn);
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
	}
	rc = hasher_finish(hasher);
	if(rc < 0) goto cleanup;

	*outstatus = status; status = 0;
	*outheaders = headers; headers = NULL;
	*outhasher = hasher; hasher = NULL;

cleanup:
	HTTPConnectionFree(&conn);
	HTTPHeadersFree(&headers);
	hasher_free(&hasher);
	return rc;
}

static int GET_index(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	if(0 != uripathcmp(URI, "/", NULL)) return -1;

	return 200;
}

static void listener(void *ctx, HTTPServerRef const server, HTTPConnectionRef const conn) {
	assert(server);
	assert(conn);
	HTTPMethod method = 99; // 0 is HTTP_DELETE...
	str_t URI[URI_MAX]; URI[0] = '\0';
	HTTPHeadersRef headers = NULL;
	ssize_t len = 0;
	int rc = 0;

	len = HTTPConnectionReadRequest(conn, &method, URI, sizeof(URI));
	if(UV_EOF == len) goto cleanup;
	if(UV_ECONNRESET == len) goto cleanup;
	if(len < 0) {
		rc = len;
		alogf("Request error: %s\n", uv_strerror(rc));
		goto cleanup;
	}

	rc = HTTPHeadersCreateFromConnection(conn, &headers);
	if(rc < 0) goto cleanup;

	strarg_t const host = HTTPHeadersGet(headers, "host");
	host_t obj[1];
	rc = host_parse(host, obj);
	// TODO: Verify Host header to prevent DNS rebinding.

/*	if(SERVER_PORT_TLS && server != server_tls) {
		rc = HTTPConnectionSendSecureRedirect(conn, obj->domain, SERVER_PORT_TLS, URI);
		goto cleanup;
	}*/

	rc = -1;
	rc = rc >= 0 ? rc : GET_index(conn, method, URI, headers);
	if(rc < 0) rc = 404;
	if(rc > 0) HTTPConnectionSendStatus(conn, rc);

cleanup:
	if(rc < 0) HTTPConnectionSendStatus(conn, HTTPError(rc));
//	char const *const username = NULL;
//	HTTPConnectionLog(conn, URI, username, headers, SERVER_LOG_FILE);
	HTTPHeadersFree(&headers);
}

static void init(void *ignore) {
	HTTPServerRef server = NULL;
	int rc;
	rc = HTTPServerCreate(listener, NULL, &server);
	if(rc < 0) goto cleanup;
	rc = HTTPServerListen(server, SERVER_RAW_ADDR, SERVER_RAW_PORT);
	if(rc < 0) goto cleanup;
	int const port = SERVER_RAW_PORT;
	alogf("Hash Archive running at http://localhost:%d/\n", port);
	server_raw = server; server = NULL;



	{ // TODO: debug
	int status = 0;
	HTTPHeadersRef headers = NULL;
	hasher_t *hasher = NULL;
	rc = url_fetch("http://localhost:8000/", &status, &headers, &hasher);
	alogf("got result %s (%d): %d, %s\n", uv_strerror(rc), rc, status, HTTPHeadersGet(headers, "content-type"));
	}


cleanup:
	HTTPServerFree(&server);
	return;
}
static void term(void *ignore) {
	HTTPServerClose(server_raw);
	HTTPServerClose(server_tls);
}
static void cleanup(void *ignore) {
	HTTPServerFree(&server_raw);
	HTTPServerFree(&server_tls);
}

int main(void) {
	int rc = async_process_init();
	if(rc < 0) {
		fprintf(stderr, "Initialization error: %s\n", uv_strerror(rc));
		return 1;
	}
/*	rc = tls_init();
	if(rc < 0) {
		fprintf(stderr, "TLS initialization error: %s\n", strerror(errno));
		return 1;
	}*/

	// Even our init code wants to use async I/O.
	async_spawn(STACK_DEFAULT, init, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	async_spawn(STACK_DEFAULT, term, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

#if 0
	// cleanup is separate from term because connections might
	// still be active.
	async_spawn(STACK_DEFAULT, cleanup, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	async_process_destroy();
#endif

	return  0;
}

