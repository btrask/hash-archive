// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <async/async.h>
#include "db.h"

static DB_env *shared_db = NULL;

int hx_db_load(void) {
	if(shared_db) return 0;
	DB_env *db = NULL;
	int rc = 0;
	rc = db_env_create(&db);
	if(rc < 0) goto cleanup;
	rc = db_env_set_mapsize(db, 1024ull*1024*1024*64); // 64GB
	if(rc < 0) goto cleanup;
	rc = db_env_open(db, "/home/user/Desktop/test.db", 0, 0600);
	if(rc < 0) goto cleanup;
	shared_db = db; db = NULL;
cleanup:
	db_env_close(db); db = NULL;
	return rc;
}
int hx_db_open(DB_env **const out) {
	assert(out);
	async_pool_enter(NULL);
	*out = shared_db;
	return 0;
}
void hx_db_close(DB_env **const in) {
	assert(in);
	DB_env *db = *in; *in = NULL;
	if(!db) return;
	db = NULL;
	async_pool_leave(NULL);
}

char const *hx_strerror(int const rc) {
	char const *x = db_strerror(rc);
	if(x) return x;
	return uv_strerror(rc);
}

