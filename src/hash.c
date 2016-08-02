// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "hash.h"

// TODO: Get rid of this duplication...
#define MIN(a, b) ({ \
	__typeof__(a) const __a = (a); \
	__typeof__(b) const __b = (b); \
	__a < __b ? __a : __b; \
})
#define MAX(a, b) ({ \
	__typeof__(a) const __a = (a); \
	__typeof__(b) const __b = (b); \
	__a > __b ? __a : __b; \
})
#define STR_LEN(x) (x), (sizeof(x)-1)

static int strnncasecmp(char const *const a, size_t const alen, char const *const b, size_t const blen) {
	if(alen != blen) return -1;
	return strncasecmp(a, b, alen);
}
size_t hash_algo_digest_len(hash_algo const algo) {
	switch(algo) {
#define XX(val, name, len, str) case HASH_ALGO_##name: return (len);
		HASH_ALGOS(XX)
#undef XX
		default: return 0;
	}
}
hash_algo hash_algo_parse(char const *const str, size_t const len) {
	if(!str) return HASH_ALGO_UNKNOWN;
#define XX(val, name, xlen, xstr) \
	if(0 == strnncasecmp(str, len, STR_LEN(xstr))) return HASH_ALGO_##name;
	HASH_ALGOS(XX)
#undef XX
	return HASH_ALGO_UNKNOWN;
}


void hash_uri_destroy(hash_uri_t *const obj) {
	obj->type = 0;
	obj->algo = 0;
	free(obj->buf); obj->buf = NULL;
	obj->len = 0;
}

static size_t match_len(regmatch_t const *const m) {
	return m->rm_eo - m->rm_so;
}
static int regcomp_err(regex_t *const preg, char const *const regex, int const cflags) {
	int rc = regcomp(preg, regex, cflags);
	if(0 == rc) return 0;
	char str[511+1];
	regerror(rc, preg, str, sizeof(str));
	fprintf(stderr, "Regex compile error: %s\n", str);
	assert(0);
	return HASH_EPANIC;
}
static int hex_decode_copy(char const *const str, size_t const len, unsigned char **const outbuf, size_t *const outlen) {
	assert(outbuf);
	assert(outlen);
	unsigned char *buf = NULL;
	ssize_t actual = 0;
	int rc = 0;
	size_t const max = (str ? strnlen(str, len) : 0) * 2;
	buf = malloc(MAX(max, 1));
	if(!buf) rc = HASH_ENOMEM;
	if(rc < 0) goto cleanup;
	actual = hex_decode(str, len, buf, max);
	if(actual < 0) rc = actual;
	if(rc < 0) goto cleanup;
	*outbuf = buf; buf = NULL;
	*outlen = actual; actual = 0;
cleanup:
	free(buf); buf = NULL;
	return rc;
}
int hash_uri_parse(char const *const URI, hash_uri_t *const out) {
	int rc = HASH_EPARSE;
	rc = rc >= 0 ? rc : hash_uri_parse_hash_uri(URI, out);
	rc = rc >= 0 ? rc : hash_uri_parse_named_info(URI, out);
	rc = rc >= 0 ? rc : hash_uri_parse_ssb(URI, out);
	rc = rc >= 0 ? rc : hash_uri_parse_magnet(URI, out);
	rc = rc >= 0 ? rc : hash_uri_parse_prefix(URI, out);
	rc = rc >= 0 ? rc : hash_uri_parse_multihash(URI, out);
	return rc;
}
int hash_uri_parse_hash_uri(char const *const URI, hash_uri_t *const out) {
	assert(out);
	if(!URI) return HASH_EINVAL;
	static regex_t re[1];
	static bool initialized = false;
	int rc = 0;
	if(!initialized) {
		rc = regcomp_err(re, "^hash://([[:alnum:].-]+)/([[:alnum:].%_-]+)(\\?[[:alnum:].%_=&-]+)?(#[[:alnum:].%_-]+)?$", REG_EXTENDED|REG_ICASE);
		if(rc < 0) return rc;
		initialized = true;
	}
	regmatch_t m[3];
	rc = regexec(re, URI, numberof(m), m, 0);
	if(0 != rc) return HASH_EPARSE;
	out->type = LINK_HASH_URI;
	out->algo = hash_algo_parse(URI+m[1].rm_so, match_len(&m[1]));
	rc = hex_decode_copy(URI+m[2].rm_so, match_len(&m[2]), &out->buf, &out->len);
	if(rc < 0) return rc;
	return 0;
}
int hash_uri_parse_prefix(char const *const URI, hash_uri_t *const out) {
	assert(out);
	if(!URI) return HASH_EINVAL;
/*	static regex_t re[1];
	static bool initialized = false;
	int rc = 0;
	if(!initialized) {
		rc = regcomp_err(re, "^([[:alnum:]]+)-([[:alnum:]/+=]+)$", REG_EXTENDED);
		if(rc < 0) return rc;
		initialized = true;
	}
	regmatch_t m[3];
	rc = regexec(re, URI, numberof(m), m, 0);
	if(0 != rc) return HASH_EPARSE;
	out->type = LINK_PREFIX;
	out->algo = hash_algo_parse(out->type, URI+m[1].rm_so, match_len(&m[1]));*/
	return HASH_EPARSE;
}
int hash_uri_parse_named_info(char const *const URI, hash_uri_t *const out) {
	assert(out);
	if(!URI) return HASH_EINVAL;
/*	static regex_t re[1];
	static bool initialized = false;
	int rc = 0;
	if(!initialized) {
		rc = regcomp_err(re, "^ni:///([[:alnum:].-]+);([[:alnum:]_-]+)$", REG_EXTENDED|REG_ICASE);
		if(rc < 0) return rc;
		initialized = true;
	}
	regmatch_t m[3];
	rc = regexec(re, URI, numberof(m), m, 0);
	if(0 != rc) return HASH_EPARSE;
	out->type = LINK_NAMED_INFO;
	out->algo = hash_algo_parse(out->type, URI+m[1].rm_so, match_len(&m[1]));*/
	return HASH_EPARSE;
}
int hash_uri_parse_multihash(char const *const URI, hash_uri_t *const out) {
	assert(out);
	if(!URI) return HASH_EINVAL;
/*	static regex_t re[1];
	static bool initialized = false;
	int rc = 0;
	if(!initialized) {
		rc = regcomp_err(re, "^[123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz]{8,}$", REG_EXTENDED);
		if(rc < 0) return rc;
		initialized = true;
	}
	regmatch_t m[3];
	rc = regexec(re, URI, numberof(m), m, 0);
	if(0 != rc) return HASH_EPARSE;
	out->type = LINK_MULTIHASH;
	out->algo = hash_algo_parse(out->type, URI+m[1].rm_so, match_len(&m[1]));*/
	return HASH_EPARSE;
}
int hash_uri_parse_ssb(char const *const URI, hash_uri_t *const out) {
	assert(out);
	if(!URI) return HASH_EINVAL;
/*	static regex_t re[1];
	static bool initialized = false;
	int rc = 0;
	if(!initialized) {
		rc = regcomp_err(re, "^&([a-zA-Z0-9+/]{8,}={0,3})\\.([a-z0-9]{3,})$", REG_EXTENDED);
		if(rc < 0) return rc;
		initialized = true;
	}
	regmatch_t m[3];
	rc = regexec(re, URI, numberof(m), m, 0);
	if(0 != rc) return HASH_EPARSE;
	out->type = LINK_SSB;
	out->algo = hash_algo_parse(out->type, URI+m[1].rm_so, match_len(&m[1]));*/
	return HASH_EPARSE;
}
int hash_uri_parse_magnet(char const *const URI, hash_uri_t *const out) {
	assert(out);
	if(!URI) return HASH_EINVAL;
/*	static regex_t re[1];
	static bool initialized = false;
	int rc = 0;
	if(!initialized) {
		rc = regcomp_err(re, "^magnet:.*(:?\\?|&)xt=urn:([a-z0-9]+):([a-z0-9]+)", REG_EXTENDED|REG_ICASE);
		if(rc < 0) return rc;
		initialized = true;
	}
	regmatch_t m[3];
	rc = regexec(re, URI, numberof(m), m, 0);
	if(0 != rc) return HASH_EPARSE;
	out->type = LINK_SSB;
	out->algo = hash_algo_parse(out->type, URI+m[1].rm_so, match_len(&m[1]));*/
	return HASH_EPARSE;
}

int hash_uri_format(hash_uri_t const *const obj, char *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	if(!obj) return HASH_EINVAL;
	if(obj->algo < 0) return HASH_EINVAL;
	if(obj->algo >= HASH_ALGO_MAX) return HASH_EINVAL;
	char const *const name = hash_algo_names[obj->algo];
	switch(obj->type) {
	case LINK_HASH_URI: {
		char hex[HASH_DIGEST_MAX*2+1];
		hex_encode(obj->buf, obj->len, hex, sizeof(hex));
		return snprintf(out, max, "hash://%s/%s", name, hex);
	} case LINK_NAMED_INFO: {
		char b64[HASH_DIGEST_MAX*4/3+1+1] = "test";
		// TODO
		return snprintf(out, max, "ni:///%s;%s", name, b64);
	} case LINK_MULTIHASH: {
		return snprintf(out, max, "test"); // TODO
	} case LINK_PREFIX: {
		char b64[HASH_DIGEST_MAX*4/3+1+1] = "test";
		// TODO
		return snprintf(out, max, "%s-%s", name, b64);
	} case LINK_SSB: {
		char b64[HASH_DIGEST_MAX*4/3+1+1] = "test";
		// TODO
		return snprintf(out, max, "&%s.%s", b64, name);
	} case LINK_MAGNET: {
		char hex[HASH_DIGEST_MAX*2+1];
		hex_encode(obj->buf, obj->len, hex, sizeof(hex)); // TODO: double-check
		return snprintf(out, max, "magnet:?xt=urn:%s:%s", name, hex);
	} default: return HASH_EINVAL;
	}
}
int hash_uri_normalize(char const *const URI, char *const out, size_t const max) {
	hash_uri_t obj[1] = {};
	int rc = hash_uri_parse(URI, obj);
	if(rc < 0) return rc;
	rc = hash_uri_format(obj, out, max);
	hash_uri_destroy(obj);
	if(rc < 0) return rc;
	return 0;
}
ssize_t hash_uri_variant(hash_uri_t const *const obj, hash_uri_type const type, char *const out, size_t const max) {
	hash_uri_t local[1] = { *obj };
	local->type = type;
	int rc = hash_uri_format(local, out, max);
	memset(local, 0, sizeof(*local));
	if(rc < 0) return rc;
	return 0;
}


static int hex_char(char const c) {
	if(c >= '0' && c <= '9') return c-'0'+0x0;
	if(c >= 'a' && c <= 'f') return c-'a'+0xa;
	if(c >= 'A' && c <= 'F') return c-'A'+0xA;
	return HASH_EINVAL;
}
size_t hex_encode(unsigned char const *const buf, size_t const len, char *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	assert(buf || 0 == len);
	char const *const map = "0123456789abcdef";
	size_t i = 0;
	for(; i < len; i++) {
		if(i*2+1 >= max) break;
		out[i*2+0] = map[buf[i] >> 4 & 0xf];
		out[i*2+1] = map[buf[i] >> 0 & 0xf];
	}
	out[i*2+0] = '\0';
	return i*2+0;
}
ssize_t hex_decode(char const *const str, size_t const len, unsigned char *const out, size_t const max) {
	assert(out);
	if(!str) return 0;
	size_t i = 0;
	for(; i < max; i++) {
		if(i*2+1 >= len) break;
		if('\0' == str[i*2+0]) break;
		if('\0' == str[i*2+1]) break;
		int const hi = hex_char(str[i*2+0]);
		int const lo = hex_char(str[i*2+1]);
		if(hi < 0) return hi;
		if(lo < 0) return lo;
		out[i] = hi << 4 | lo << 0;
	}
	return i;
}

