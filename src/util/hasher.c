// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdlib.h>
#include <openssl/sha.h>
#include "hash.h"

typedef SHA_CTX SHA1_CTX;
typedef SHA512_CTX SHA384_CTX;

struct hasher_s {
	void *state[HASH_ALGO_MAX];
};

int hasher_create(uint64_t const algos, hasher_t **const out) {
	assert(out);
	hasher_t *hasher = calloc(1, sizeof(struct hasher_s));
	if(!hasher) return HASH_ENOMEM;
	int rc = 0;
#define XX(val, name, len, str) \
	if(1 << (val) & algos) { \
		hasher->state[(val)] = malloc(sizeof(name##_CTX)); \
		if(!hasher->state[(val)]) rc = HASH_ENOMEM; \
		if(rc < 0) goto cleanup; \
		rc = name##_Init(hasher->state[(val)]); \
		if(rc < 0) goto cleanup; \
	}
	HASH_ALGOS(XX)
#undef XX
	*out = hasher; hasher = NULL;
cleanup:
	hasher_free(&hasher);
	return rc;
}
void hasher_free(hasher_t **const hasherptr) {
	hasher_t *hasher = *hasherptr; *hasherptr = NULL;
	if(!hasher) return;
#define XX(val, name, len, str) \
	free(hasher->state[(val)]); hasher->state[(val)] = NULL;
	HASH_ALGOS(XX)
#undef XX
	free(hasher); hasher = NULL;
}
int hasher_update(hasher_t *const hasher, unsigned char const *const buf, size_t const len) {
	if(!hasher) return 0;
#define XX(val, name, len, str) \
	if(hasher->state[(val)]) { \
		int rc = name##_Update(hasher->state[(val)], buf, len); \
		if(rc < 0) return rc; \
	}
	HASH_ALGOS(XX)
#undef XX
	return 0;
}
int hasher_digests(hasher_t *const hasher, hash_digest_t *const out, size_t const count) {
	if(!hasher) return 0;
#define XX(val, name, xlen, str) \
	if((val) < count && hasher->state[(val)]) { \
		int rc = name##_Final(out[(val)].buf, hasher->state[(val)]); \
		if(rc < 0) return rc; \
		out[(val)].len = (xlen); \
	}
	HASH_ALGOS(XX)
#undef XX
	for(size_t i = HASH_ALGO_MAX; i < count; i++) {
		out[i].len = 0;
	}
	return 0;
}

