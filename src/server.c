// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <async/async.h>
#include <async/http/HTTPServer.h>
#include "strext.h"

#define SERVER_RAW_ADDR NULL
#define SERVER_RAW_PORT 8000

static HTTPServerRef server_raw = NULL;
static HTTPServerRef server_tls = NULL;

static void listener(void *const context, HTTPServerRef const server, HTTPConnectionRef const conn) {
	HTTPConnectionSendStatus(conn, 200);
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
	// TODO
//	raiserlimit();
	async_init();

	// TODO
/*	int rc = tls_init();
	if(rc < 0) {
		fprintf(stderr, "TLS initialization error: %s\n", strerror(errno));
		return 1;
	}*/

	// Even our init code wants to use async I/O.
	async_spawn(STACK_DEFAULT, init, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	async_spawn(STACK_DEFAULT, term, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	// cleanup is separate from term because connections might
	// still be active.
	async_spawn(STACK_DEFAULT, cleanup, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	async_destroy();

	return  0;
}

