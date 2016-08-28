// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <async/async.h>
#include "util/hash.h"
#include "db.h"

#define SOCKET_PATH "./import.sock"
#define RESPONSE_BATCH_SIZE 50

static int read_len(uv_stream_t *const stream, unsigned char *const out, size_t const len) {
	// TODO: fix async_read?
	assert(out);
	if(!len) return 0;
	uv_buf_t buf[1];
	int rc;
	do rc = async_read(stream, len, buf);
	while(UV_EAGAIN == rc); // WTF?
	if(rc < 0) return rc;
	if(buf->len < len) rc = UV_EOF;
	if(rc < 0) goto cleanup;
	memcpy(out, buf->base, len);
cleanup:
	free(buf->base); buf->base = NULL;
	return rc;
}
static int read_uint16(uv_stream_t *const stream, uint16_t *const out) {
	assert(out);
	unsigned char x[2];
	int rc = read_len(stream, x, sizeof(x));
	if(rc < 0) return rc;
	*out =
		(uint16_t)x[0] << 8 |
		(uint16_t)x[1] << 0;
	return 0;
}
static int read_uint64(uv_stream_t *const stream, uint64_t *const out) {
	assert(out);
	unsigned char x[8];
	int rc = read_len(stream, x, sizeof(x));
	if(rc < 0) return rc;
	*out =
		(uint64_t)x[0] << 56 |
		(uint64_t)x[1] << 48 |
		(uint64_t)x[2] << 40 |
		(uint64_t)x[3] << 32 |
		(uint64_t)x[4] << 24 |
		(uint64_t)x[5] << 16 |
		(uint64_t)x[6] <<  8 |
		(uint64_t)x[7] <<  0;
	return 0;
}
static int read_string(uv_stream_t *const stream, char *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	uint16_t len = 0;
	int rc = read_uint16(stream, &len);
	if(rc < 0) return rc;
	if(len+1 > max) return UV_EMSGSIZE;
	rc = read_len(stream, (unsigned char *)out, len);
	if(rc < 0) return rc;
	out[len] = '\0';
	return 0;
}
static ssize_t read_blob(uv_stream_t *const stream, unsigned char *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	uint16_t len = 0;
	int rc = read_uint16(stream, &len);
	if(rc < 0) return rc;
	if(len > max) return UV_EMSGSIZE;
	rc = read_len(stream, out, len);
	if(rc < 0) return rc;
	return len;
}
static ssize_t read_responses(uv_stream_t *const stream, struct response *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	if(!stream) return UV_EINVAL;
	size_t x = 0;
	for(; x < max; x++) {
		uint64_t tmp;
		uint16_t hcount;
		int rc = read_uint64(stream, &out[x].time);
		if(rc < 0) goto cleanup;
		rc = read_string(stream, out[x].url, sizeof(out[x].url));
		if(rc < 0) goto cleanup;
		rc = read_uint64(stream, &tmp);
		if(rc < 0) goto cleanup;
		out[x].status = tmp - 0xffff;
		rc = read_string(stream, out[x].type, sizeof(out[x].type));
		if(rc < 0) goto cleanup;
		rc = read_uint64(stream, &out[x].length);
		if(rc < 0) goto cleanup;

		rc = read_uint16(stream, &hcount);
		if(rc < 0) goto cleanup;
		for(size_t i = 0; i < MIN(hcount, HASH_ALGO_MAX); i++) {
			ssize_t len = read_blob(stream, out[x].digests[i].buf, HASH_DIGEST_MAX);
			if(len < 0) rc = len;
			if(rc < 0) goto cleanup;
			out[x].digests[i].len = len;
		}
		for(size_t i = HASH_ALGO_MAX; i < hcount; i++) {
			unsigned char discard[HASH_DIGEST_MAX];
			ssize_t len = read_blob(stream, discard, HASH_DIGEST_MAX);
			if(len < 0) rc = len;
			if(rc < 0) goto cleanup;
		}
		for(size_t i = hcount; i < HASH_ALGO_MAX; i++) {
			out[x].digests[i].len = 0;
		}
	}
cleanup:
	return x;
}


static void connection(void *arg) {
	uv_stream_t *const server = arg;
	uv_pipe_t pipe[1];
	uv_stream_t *const stream = (uv_stream_t *)pipe;
	DB_env *db = NULL;
	DB_txn *txn = NULL;
	struct response *responses = NULL;
	uint64_t id = 0;

	int rc = uv_pipe_init(async_loop, pipe, false);
	if(rc < 0) goto cleanup;
	rc = uv_accept(server, stream);
	if(rc < 0) goto cleanup;

	responses = calloc(RESPONSE_BATCH_SIZE, sizeof(struct response));
	if(!responses) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;

	for(;;) {

		ssize_t count = read_responses(stream, responses, RESPONSE_BATCH_SIZE);
		if(count < 0) rc = count;
		if(rc < 0) goto cleanup;

		rc = hx_db_open(&db);
		if(rc < 0) goto cleanup;
		rc = db_txn_begin(db, NULL, DB_RDWR, &txn);
		if(rc < 0) goto cleanup;

		for(size_t i = 0; i < count; i++) {
			rc = hx_response_add(txn, &responses[i], id++);
			if(rc < 0) goto cleanup;
		}

		rc = db_txn_commit(txn); txn = NULL;
		if(rc < 0) goto cleanup;
		hx_db_close(&db);

		fprintf(stderr, "Imported %zu\n", (size_t)count);

		if(count < RESPONSE_BATCH_SIZE) rc = UV_EOF;
		if(rc < 0) goto cleanup;

	}

cleanup:
	async_close((uv_handle_t *)pipe);
	db_txn_abort(txn); txn = NULL;
	hx_db_close(&db);
	fprintf(stderr, "Import ended: %s\n", hx_strerror(rc));
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

