// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

// Max value is 63.
//	XX( n, name,    len)
#define HASHER_ALGOS(XX) \
/*	XX( 0, MD5,     16)*/ \
	XX( 1, SHA1,    20) \
	XX( 2, SHA256,  32) \
	XX( 3, SHA384,  48) \
	XX( 4, SHA512,  64) \
/*	XX( 5, BLAKE2S, 32)*/ \
/*	XX( 6, BLAKE2B, 64)*/

enum {
#define XX(val, name, len) HASHER_ALGO_##name = (val),
	HASHER_ALGOS(XX)
#undef XX
	HASHER_ALGO_MAX,
};
enum {
	HASHER_ALGOS_ALL = UINT64_MAX,
};
enum {
	HASHER_EINVAL = -EINVAL,
	HASHER_ENOMEM = -ENOMEM,
};

typedef struct hasher_s hasher_t;

int hasher_create(uint64_t const algos, hasher_t **const out);
void hasher_free();
int hasher_update(hasher_t *const hasher, unsigned char const *const buf, size_t const len);
int hasher_finish(hasher_t *const hasher);
uint8_t const *hasher_get(hasher_t *const hasher, int const algo);
size_t hasher_len(int const algo);

