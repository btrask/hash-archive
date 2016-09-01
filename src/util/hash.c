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
// TODO: Copy and paste...
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
static int b64_decode_copy(char const *const str, size_t const len, unsigned char **const outbuf, size_t *const outlen) {
	assert(outbuf);
	assert(outlen);
	unsigned char *buf = NULL;
	ssize_t actual = 0;
	int rc = 0;
	size_t const max = (str ? strnlen(str, len) : 0) * 4/3;
	buf = malloc(MAX(max, 1));
	if(!buf) rc = HASH_ENOMEM;
	if(rc < 0) goto cleanup;
	actual = b64_decode(str, len, buf, max);
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
	// TODO
//	rc = rc >= 0 ? rc : hash_uri_parse_multihash(URI, out);
	return rc;
}
int hash_uri_parse_hash_uri(char const *const URI, hash_uri_t *const out) {
	assert(out);
	if(!URI) return HASH_EINVAL;
	static regex_t re[1];
	static bool initialized = false;
	int rc = 0;
	if(!initialized) {
		rc = regcomp_err(re, "^hash://([[:alnum:].-]+)/([[:alnum:].%_-]*)(\\?[[:alnum:].%_=&-]+)?(#[[:alnum:].%_-]+)?$", REG_EXTENDED|REG_ICASE);
		if(rc < 0) return rc;
		initialized = true;
	}
	regmatch_t m[1+2];
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
	static regex_t re[1];
	static bool initialized = false;
	int rc = 0;
	if(!initialized) {
		rc = regcomp_err(re, "^([[:alnum:]]+)-([[:alnum:]/+=]*)$", REG_EXTENDED);
		if(rc < 0) return rc;
		initialized = true;
	}
	regmatch_t m[1+2];
	rc = regexec(re, URI, numberof(m), m, 0);
	if(0 != rc) return HASH_EPARSE;
	out->type = LINK_PREFIX;
	out->algo = hash_algo_parse(URI+m[1].rm_so, match_len(&m[1]));
	rc = b64_decode_copy(URI+m[2].rm_so, match_len(&m[2]), &out->buf, &out->len);
	if(rc < 0) return rc;
	return 0;
}
int hash_uri_parse_named_info(char const *const URI, hash_uri_t *const out) {
	assert(out);
	if(!URI) return HASH_EINVAL;
	static regex_t re[1];
	static bool initialized = false;
	int rc = 0;
	if(!initialized) {
		rc = regcomp_err(re, "^ni:///([[:alnum:].-]+);([[:alnum:]_-]*)$", REG_EXTENDED|REG_ICASE);
		if(rc < 0) return rc;
		initialized = true;
	}
	regmatch_t m[1+2];
	rc = regexec(re, URI, numberof(m), m, 0);
	if(0 != rc) return HASH_EPARSE;
	out->type = LINK_NAMED_INFO;
	out->algo = hash_algo_parse(URI+m[1].rm_so, match_len(&m[1]));
	rc = b64_decode_copy(URI+m[2].rm_so, match_len(&m[2]), &out->buf, &out->len);
	if(rc < 0) return rc;
	return 0;
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
	out->algo = hash_algo_parse(URI+m[1].rm_so, match_len(&m[1]));
//	rc = b64_decode_copy(URI+m[2].rm_so, match_len(&m[2]), &out->buf, &out->len);
	if(rc < 0) return rc;*/
	return -1;
}
int hash_uri_parse_ssb(char const *const URI, hash_uri_t *const out) {
	assert(out);
	if(!URI) return HASH_EINVAL;
	static regex_t re[1];
	static bool initialized = false;
	int rc = 0;
	if(!initialized) {
		rc = regcomp_err(re, "^&([a-zA-Z0-9+/]*={0,3})\\.([a-z0-9]{3,})$", REG_EXTENDED);
		if(rc < 0) return rc;
		initialized = true;
	}
	regmatch_t m[1+2];
	rc = regexec(re, URI, numberof(m), m, 0);
	if(0 != rc) return HASH_EPARSE;
	out->type = LINK_SSB;
	out->algo = hash_algo_parse(URI+m[2].rm_so, match_len(&m[2]));
	rc = b64_decode_copy(URI+m[1].rm_so, match_len(&m[1]), &out->buf, &out->len);
	if(rc < 0) return rc;
	return 0;
}
int hash_uri_parse_magnet(char const *const URI, hash_uri_t *const out) {
	assert(out);
	if(!URI) return HASH_EINVAL;
	static regex_t re[1];
	static bool initialized = false;
	int rc = 0;
	if(!initialized) {
		rc = regcomp_err(re, "^magnet:.*(\\?|&)xt=urn:([a-z0-9]+):([a-z0-9]*)", REG_EXTENDED|REG_ICASE);
		if(rc < 0) return rc;
		initialized = true;
	}
	regmatch_t m[1+3];
	rc = regexec(re, URI, numberof(m), m, 0);
	if(0 != rc) return HASH_EPARSE;
	out->type = LINK_MAGNET;
	out->algo = hash_algo_parse(URI+m[2].rm_so, match_len(&m[2]));
	rc = hex_decode_copy(URI+m[3].rm_so, match_len(&m[3]), &out->buf, &out->len);
	if(rc < 0) return rc;
	return 0;
}

int hash_uri_format(hash_uri_t const *const obj, char *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	if(!obj) return HASH_EINVAL;
	if(obj->algo < 0) return HASH_EINVAL;
	if(obj->algo >= HASH_ALGO_MAX) return HASH_EINVAL;
	char const *const name = hash_algo_names[obj->algo];
	if(!name) return HASH_EINVAL;
	switch(obj->type) {
	case LINK_HASH_URI: {
		char hex[HASH_DIGEST_MAX*2+1];
		hex_encode(obj->buf, obj->len, hex, sizeof(hex));
		return snprintf(out, max, "hash://%s/%s", name, hex);
	} case LINK_NAMED_INFO: {
		char b64[HASH_DIGEST_MAX*4/3+1+1] = "test";
		b64_encode(B64_URL, obj->buf, obj->len, b64, sizeof(b64));
		return snprintf(out, max, "ni:///%s;%s", name, b64);
	} case LINK_MULTIHASH: {
		return snprintf(out, max, "[TODO]"); // TODO
	} case LINK_PREFIX: {
		char b64[HASH_DIGEST_MAX*4/3+1+1];
		b64_encode(B64_STD, obj->buf, obj->len, b64, sizeof(b64));
		return snprintf(out, max, "%s-%s", name, b64);
	} case LINK_SSB: {
		char b64[HASH_DIGEST_MAX*4/3+1+1] = "test";
		b64_encode(B64_STD, obj->buf, obj->len, b64, sizeof(b64));
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
	for(; i*2+1+1 < max; i++) {
		if(i >= len) break;
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

// Based on:
// https://en.wikipedia.org/wiki/Base64
// https://en.wikipedia.org/w/index.php?title=Base64&oldid=731456847
static bool b64_term(char const c) {
	return '\0' == c || '=' == c;
}
static int b64_char(char const c) {
	if(c >= 'A' && c <= 'Z') return c-'A'+0;
	if(c >= 'a' && c <= 'z') return c-'a'+26;
	if(c >= '0' && c <= '9') return c-'0'+52;
	if('+' == c || '-' == c) return 62;
	if('/' == c || '_' == c) return 63;
	return HASH_EINVAL;
}
size_t b64_encode(b64_type const type, unsigned char const *const buf, size_t const len, char *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	assert(buf || 0 == len);
	static char const *const b64_std =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";
	static char const *const b64_url =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789-_";
	char const *const map = (B64_URL == type ? b64_url : b64_std);
	bool const extend = B64_STD == type;
	size_t i = 0;
	int b;
	for(;; i++) {
		out[i*4+0] = '\0';
		if(i*4+3+1 >= max) return i*4+0;
		if(i*3+0 >= len) return i*4+0;
		b = (buf[i*3+0]&0xfc) >> 2;
		out[i*4+0] = map[b];
		b = (buf[i*3+0]&0x03) << 4;
		out[i*4+1] = map[b];
		out[i*4+2] = extend ? '=' : '\0';
		out[i*4+3] = extend ? '=' : '\0';
		out[i*4+3+1] = '\0';
		if(i*3+1 >= len) return extend ? i*4+3+1 : i*4+1+1;
		b |= (buf[i*3+1]&0xf0) >> 4;
		out[i*4+1] = map[b];
		b = (buf[i*3+1]&0x0f) << 2;
		out[i*4+2] = map[b];
		if(i*3+2 >= len) return extend ? i*4+3+1 : i*4+2+1;
		b |= (buf[i*3+2]&0xc0) >> 6;
		out[i*4+2] = map[b];
		b = (buf[i*3+2]&0x3f) >> 0;
		out[i*4+3] = map[b];
	}
	assert(0);
	return HASH_EPANIC;
}
ssize_t b64_decode(char const *const str, size_t const len, unsigned char *const out, size_t const max) {
	assert(out);
	if(!str) return 0;
	size_t i = 0;
	int hi, lo;
	for(;; i++) {
		if(i*3+0 >= max) return i*3+0;
		if(i*4+0 >= len || b64_term(str[i*4+0])) return i*3+0;
		hi = 0;
		lo = b64_char(str[i*4+0]);
		if(lo < 0) return lo;
		// No assignment
		//if(i*3+0 >= max) return i*3+0;
		if(i*4+1 >= len || b64_term(str[i*4+1])) return i*3+0;
		hi = lo;
		lo = b64_char(str[i*4+1]);
		if(lo < 0) return lo;
		out[i*3+0] = hi << 2 | lo >> 4;
		if(i*3+1 >= max) return i*3+1;
		if(i*4+2 >= len || b64_term(str[i*4+2])) return i*3+1;
		hi = lo;
		lo = b64_char(str[i*4+2]);
		if(lo < 0) return lo;
		out[i*3+1] = hi << 4 | lo >> 2;
		if(i*3+2 >= max) return i*3+2;
		if(i*4+3 >= len || b64_term(str[i*4+3])) return i*3+2;
		hi = lo;
		lo = b64_char(str[i*4+3]);
		if(lo < 0) return lo;
		out[i*3+2] = hi << 6 | lo >> 0;
	}
	assert(0);
	return HASH_EPANIC;
}

