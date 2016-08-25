// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <async/async.h>

#define SOCKET_PATH "./import.sock"
#define BUF_SIZE (1024*8)

static void connection(void *arg) {
	uv_stream_t *const server = arg;
	uv_pipe_t conn[1];
	int rc = uv_pipe_init(async_loop, conn, false);
	if(rc < 0) goto cleanup;
	rc = uv_accept(server, (uv_stream_t *)conn);
	if(rc < 0) goto cleanup;

	for(;;) {
		uv_buf_t buf[1];
		rc = async_read((uv_stream_t *)conn, BUF_SIZE, buf);
		if(rc < 0) break;
		fprintf(stderr, "read %zu\n", buf->len);
		free(buf->base); buf->base = NULL;
	}

cleanup:
	async_close((uv_handle_t *)conn);
	fprintf(stderr, "closed\n");
}
static void connection_cb(uv_stream_t *const server, int const status) {
	async_spawn(STACK_DEFAULT, connection, server);
}
int import_init(void) {
	static uv_pipe_t pipe[1];

	int rc = uv_pipe_init(async_loop, pipe, false);
	if(rc < 0) goto cleanup;

	async_fs_unlink(SOCKET_PATH);
	rc = uv_pipe_bind(pipe, SOCKET_PATH);
	if(rc < 0) goto cleanup;

	rc = uv_listen((uv_stream_t *)pipe, 511, connection_cb);
	if(rc < 0) goto cleanup;

cleanup:
	return rc;
}

