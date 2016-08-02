// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdlib.h>
#include <openssl/sha.h>
#include "hash.h"

typedef SHA_CTX SHA1_CTX;
typedef SHA512_CTX SHA384_CTX;

enum {
	HASHER_FINISHED = 1 << 1,
};

struct hasher_s {
	unsigned flags;
	void *state[HASH_ALGO_MAX];
};

int hasher_create(uint64_t const algos, hasher_t **const out) {
	assert(out);
	hasher_t *hasher = calloc(1, sizeof(struct hasher_s));
	if(!hasher) return HASH_ENOMEM;
	int rc = 0;
	hasher->flags = 0;
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
	hasher->flags = 0;
#define XX(val, name, len, str) \
	free(hasher->state[(val)]); hasher->state[(val)] = NULL;
	HASH_ALGOS(XX)
#undef XX
	free(hasher); hasher = NULL;
}
int hasher_update(hasher_t *const hasher, unsigned char const *const buf, size_t const len) {
	if(!hasher) return 0;
	if(HASHER_FINISHED & hasher->flags) return HASH_EINVAL;
#define XX(val, name, len, str) \
	if(hasher->state[(val)]) { \
		int rc = name##_Update(hasher->state[(val)], buf, len); \
		if(rc < 0) return rc; \
	}
	HASH_ALGOS(XX)
#undef XX
	return 0;
}
int hasher_finish(hasher_t *const hasher) {
	if(!hasher) return 0;
	if(HASHER_FINISHED & hasher->flags) return HASH_EINVAL;
	hasher->flags |= HASHER_FINISHED;
#define XX(val, name, len, str) \
	if(hasher->state[(val)]) { \
		uint8_t *tmp = malloc((len)); \
		if(!tmp) return HASH_ENOMEM; \
		int rc = name##_Final(tmp, hasher->state[(val)]); \
		free(hasher->state[(val)]); hasher->state[(val)] = tmp; tmp = NULL; \
		if(rc < 0) return rc; \
	}
	HASH_ALGOS(XX)
#undef XX
	return 0;
}
uint8_t const *hasher_get(hasher_t *const hasher, int const algo) {
	if(!hasher) return NULL;
	if(!(HASHER_FINISHED & hasher->flags)) return NULL;
	switch(algo) {
#define XX(val, name, len, str) case HASH_ALGO_##name: return hasher->state[(val)];
		HASH_ALGOS(XX)
#undef XX
		default: return NULL;
	}
}
size_t hasher_len(int const algo) {
	switch(algo) {
#define XX(val, name, len, str) case HASH_ALGO_##name: return (len);
		HASH_ALGOS(XX)
#undef XX
		default: return 0;
	}
}
