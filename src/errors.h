// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <kvstore/db_base.h>
#include <async/http/HTTP.h>
#include "util/hash.h"

// Note: These errors are part of the database dump format!

#define HX_ERRORS(XX) \
	XX(-12400, UNKNOWN, "unknown error") \
	XX(-12401, BLOCKED, "blocked") \
	XX(-12402, NOTFOUND, "not found") \
	XX(-12403, CONNREFUSED, "connection refused") \
	XX(-12404, REDIRECT, "too many redirects") \
	XX(-12405, TRUNCATED, "truncated response") \
	XX(-12406, TIMEDOUT, "connection timed out") \
	\
	XX(-12501, CERT_HAS_EXPIRED, "certificate expired") \
	XX(-12502, UNABLE_TO_VERIFY_LEAF_SIGNATURE, "unable to verify leaf signature") \
	XX(-12503, UNABLE_TO_GET_ISSUER_CERT, "unable to get issuer cert") \
	XX(-12504, UNABLE_TO_GET_CRL, "unable to get certificate revocation list") \
	XX(-12505, UNABLE_TO_DECRYPT_CERT_SIGNATURE, "unable to decrypt certificate signature") \
	XX(-12506, UNABLE_TO_DECRYPT_CRL_SIGNATURE, "unable to decrypt CRL signature") \
	XX(-12507, UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY, "unable to decode issuer public key") \
	XX(-12508, CERT_SIGNATURE_FAILURE, "certificate signature failure") \
	XX(-12509, CRL_SIGNATURE_FAILURE, "CRL signature failure") \
	XX(-12510, CERT_NOT_YET_VALID, "certificate not yet valid") \
	XX(-12511, CRL_NOT_YET_VALID, "CRL not yet valid") \
	XX(-12512, CRL_HAS_EXPIRED, "CRL expired") \
	XX(-12513, ERROR_IN_CERT_NOT_BEFORE_FIELD, "error in certificate not before field") \
	XX(-12514, ERROR_IN_CERT_NOT_AFTER_FIELD, "error in certificate not after field") \
	XX(-12515, ERROR_IN_CRL_LAST_UPDATE_FIELD, "error in CRL last update field") \
	XX(-12516, ERROR_IN_CRL_NEXT_UPDATE_FIELD, "error in CRL next update field") \
	XX(-12517, OUT_OF_MEM, "out of memory") \
	XX(-12518, DEPTH_ZERO_SELF_SIGNED_CERT, "depth zero self-signed certificate") \
	XX(-12519, SELF_SIGNED_CERT_IN_CHAIN, "self-signed certificate in chain") \
	XX(-12520, UNABLE_TO_GET_ISSUER_CERT_LOCALLY, "unable to get issuer certificate locally") \
	XX(-12521, CERT_CHAIN_TOO_LONG, "certificate chain too long") \
	XX(-12522, CERT_REVOKED, "certificate revoked") \
	XX(-12523, INVALID_CA, "invalid certificate authority") \
	XX(-12524, PATH_LENGTH_EXCEEDED, "path length exceeded") \
	XX(-12525, INVALID_PURPOSE, "invalid purpose") \
	XX(-12526, CERT_UNTRUSTED, "certificate untrusted") \
	XX(-12527, CERT_REJECTED, "certificate rejected")

enum {
#define XX(val, name, str) HX_ERR_##name = (val),
	HX_ERRORS(XX)
#undef XX
};

static char const *hx_strerror(int const rc) {
	if(rc >= 0) return "No error";
	switch(rc) {
#define XX(val, name, str) case HX_ERR_##name: return (str);
	HX_ERRORS(XX)
#undef XX
	case HASH_EPANIC: return "Hash panic";
	case HASH_EPARSE: return "Hash parse error";
	}
	char const *x = db_strerror(rc);
	if(x) return x;
	return uv_strerror(rc);
}
static int hx_httperr(int const rc) {
	if(rc >= 0) return 0; // Not expected
	switch(rc) {
	case HASH_EPANIC: return 500;
	case HASH_EPARSE: return 400;
	}
	return HTTPError(rc);
}

