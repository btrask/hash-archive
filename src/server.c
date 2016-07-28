// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <async/async.h>

static void init(void *ignore) {

}
static void term(void *ignore) {

}
static void cleanup(void *ignore) {

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

