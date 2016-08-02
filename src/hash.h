// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#ifndef HASH_H
#define HASH_H

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#ifndef numberof
#define numberof(x) (sizeof(x) / sizeof(*(x)))
#endif

// Max value is 63 to fit uint64_t bitmask.
//	XX( n, name,    len, str)
#define HASH_ALGOS(XX) \
/*	XX( 0, MD5,     16, "md5")*/ \
	XX( 1, SHA1,    20, "sha1") \
	XX( 2, SHA256,  32, "sha256") \
	XX( 3, SHA384,  48, "sha384") \
	XX( 4, SHA512,  64, "sha512") \
/*	XX( 5, BLAKE2S, 32, "blake2s")*/ \
/*	XX( 6, BLAKE2B, 64, "blake2b")*/

#define HASH_DIGEST_MAX 64

static char const *const hash_algo_names[] = {
#define XX(val, name, len, str) [(val)] = str,
	HASH_ALGOS(XX)
#undef XX
};
typedef enum {
	HASH_ALGO_UNKNOWN = -1,
#define XX(val, name, len, str) HASH_ALGO_##name = (val),
	HASH_ALGOS(XX)
#undef XX
	HASH_ALGO_MAX = numberof(hash_algo_names),
} hash_algo;
size_t hash_algo_digest_len(hash_algo const algo);
hash_algo hash_algo_parse(char const *const str, size_t const len);

typedef enum {
	LINK_NONE = 0, // Orindary string, not a link
	LINK_RAW = 1, // A link that shouldn't be transformed
	LINK_WEB_URL = 2, // http:, https:, ftp:
	LINK_HASH_URI = 3,
	LINK_NAMED_INFO = 4,
	LINK_MULTIHASH = 5,
	LINK_PREFIX = 6,
	LINK_SSB = 7,
	LINK_MAGNET = 8,
} hash_uri_type;
typedef struct {
	hash_uri_type type;
	hash_algo algo;
	unsigned char *buf;
	size_t len;
} hash_uri_t;
void hash_uri_destroy(hash_uri_t *const obj);
int hash_uri_parse(char const *const URI, hash_uri_t *const out);
int hash_uri_parse_hash_uri(char const *const URI, hash_uri_t *const out);
int hash_uri_parse_prefix(char const *const URI, hash_uri_t *const out);
int hash_uri_parse_named_info(char const *const URI, hash_uri_t *const out);
int hash_uri_parse_multihash(char const *const URI, hash_uri_t *const out);
int hash_uri_parse_ssb(char const *const URI, hash_uri_t *const out);
int hash_uri_parse_magnet(char const *const URI, hash_uri_t *const out);
int hash_uri_format(hash_uri_t const *const obj, char *const out, size_t const max);
int hash_uri_normalize(char const *const URI, char *const out, size_t const max);
ssize_t hash_uri_variant(hash_uri_t const *const obj, hash_uri_type const type, char *const out, size_t const max);

size_t hex_encode(unsigned char const *const buf, size_t const len, char *const out, size_t const max);
ssize_t hex_decode(char const *const str, size_t const len, unsigned char *const out, size_t const max);
// hex_decode is intended to reliably parse untrusted input.
// In particular, it handles odd-length hex strings and non-hex characters.

// hasher.c
enum {
	HASHER_ALGOS_ALL = UINT64_MAX,
};
typedef struct hasher_s hasher_t;
int hasher_create(uint64_t const algos, hasher_t **const out);
void hasher_free();
int hasher_update(hasher_t *const hasher, unsigned char const *const buf, size_t const len);
int hasher_finish(hasher_t *const hasher);
uint8_t const *hasher_get(hasher_t *const hasher, int const algo);
size_t hasher_len(int const algo);

enum {
	HASH_EINVAL = -EINVAL,
	HASH_ENOMEM = -ENOMEM,
	HASH_EPANIC = -15701,
	HASH_EPARSE = -15702,
};
static char const *hash_strerror(int const rc) {
	switch(rc) {
	case HASH_EINVAL: return "Invalid argument";
	case HASH_ENOMEM: return "No memory";
	case HASH_EPANIC: return "Hash panic";
	case HASH_EPARSE: return "Hash parse error";
	default: return NULL;
	}
}

#endif
