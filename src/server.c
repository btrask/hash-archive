// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdlib.h>
#include <async/async.h>
#include <async/http/HTTPServer.h>
#include "util/strext.h"
#include "util/hash.h"
#include "util/url.h"
#include "page.h"

#define SERVER_RAW_ADDR NULL
#define SERVER_RAW_PORT 8000

static HTTPServerRef server_raw = NULL;
static HTTPServerRef server_tls = NULL;

// TODO: Remember that we should use the archive's URL representation in the DB.
// http://com,example,www/ or something like that


// TODO
int url_fetch(strarg_t const URL, int *const outstatus, HTTPHeadersRef *const outheaders, hasher_t **const outhasher);


static int GET_index(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	if(0 != uripathcmp(URI, "/", NULL)) return -1;
	return HTTPError(page_index(conn));
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
	async_pool_destroy_shared();
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

