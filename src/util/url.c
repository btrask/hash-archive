// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "url.h"

#define numberof(x) (sizeof(x) / sizeof(*(x)))

static void strlower(char *const str) {
	assert(str);
	for(size_t i = 0; '\0' != str[i]; i++) {
		char const c = str[i];
		if(c >= 'A' && c <= 'Z') str[i] += 'a'-'A'; // 0x20
	}
}
int url_parse(char const *const URL, url_t *const out) {
	assert(out);
	if(!URL) return URL_EINVAL;
	out->scheme[0] = '\0';
	out->host[0] = '\0';
	out->path[0] = '\0';
	out->query[0] = '\0';
	if('/' == URL[0] && '/' == URL[1]) {
		// Scheme-relative
		sscanf(URL, "//" URL_HOST_FMT URL_PATH_FMT URL_QUERY_FMT,
			out->host, out->path, out->query);
		if('\0' == out->host[0]) return URL_EPARSE;
	} else if('/' == URL[0]) {
		// Host-relative
		sscanf(URL, URL_PATH_FMT URL_QUERY_FMT,
			out->path, out->query);
		if('/' != out->path[0]) return URL_EPARSE;
	} else {
		// Absolute
		sscanf(URL, URL_SCHEME_FMT "://" URL_HOST_FMT URL_PATH_FMT URL_QUERY_FMT,
			out->scheme, out->host, out->path, out->query);
		if('\0' == out->scheme[0]) return URL_EPARSE;
		if('\0' == out->host[0]) return URL_EPARSE;
	}
	if('\0' != out->path[0] && '/' != out->path[0]) return URL_EPARSE;
	if('\0' != out->query[0] && '?' != out->query[0]) return URL_EPARSE;
	strlower(out->scheme);
	strlower(out->host);
	return 0;
}
int url_format(url_t const *const URL, char *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	if(!URL) return URL_EINVAL;
	int rc = snprintf(out, max, "%s://%s%s%s",
		URL->scheme, URL->host, URL->path, URL->query);
	if(rc >= max) return -ENAMETOOLONG;
	if(rc < 0) return rc;
	return 0;
}

int host_parse(char const *const host, host_t *const out) {
	assert(out);
	if(!host) return URL_EINVAL;
	out->domain[0] = '\0';
	out->port = 0;
	sscanf(host, URL_DOMAIN_FMT ":%u", out->domain, &out->port);
	if('\0' == out->domain[0]) return URL_EPARSE;
	if(out->port > UINT16_MAX) return URL_EPARSE;
	return 0;
}
int host_format(host_t const *const host, char *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	if(!host) return URL_EINVAL;
	int rc = 0;
	if(host->port) {
		rc = snprintf(out, max, "%s:%u", host->domain, host->port);
	} else {
		rc = snprintf(out, max, "%s", host->domain);
	}
	if(rc >= max) return -ENAMETOOLONG;
	if(rc < 0) return rc;
	return 0;
}

int url_normalize(char const *const URL, char *const out, size_t const max) {
	url_t obj[1] = {};
	int rc = url_parse(URL, obj);
	if(rc < 0) return rc;
	rc = url_format(obj, out, max);
	if(rc < 0) return rc;
	return 0;
}

#define SUBDOMAIN_FMT "%m[a-z0-9-]"
static int domain_reverse(char const *const domain, char *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	if(!domain) return URL_EINVAL;
	// TODO:
	// 1. Silently truncates domains with more than 8 levels
	// 2. Silently truncates broken domains
	// 3. Doesn't do much validation.
	char *d[8] = {};
	sscanf(domain,
		SUBDOMAIN_FMT "."
		SUBDOMAIN_FMT "."
		SUBDOMAIN_FMT "."
		SUBDOMAIN_FMT "."
		SUBDOMAIN_FMT "."
		SUBDOMAIN_FMT "."
		SUBDOMAIN_FMT "."
		SUBDOMAIN_FMT,
		&d[0], &d[1], &d[2], &d[3],
		&d[4], &d[5], &d[6], &d[7]
	);
	strlcpy(out, "(", max);
	for(size_t i = numberof(d); i-- > 0;) {
		if(!d[i]) continue;
		strlcat(out, d[i], max);
		strlcat(out, ",", max);
		free(d[i]); d[i] = NULL;
	}
	strlcat(out, ")", max);
	return 0;
}
int url_normalize_surt(char const *const URL, char *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	if(!URL) return URL_EINVAL;
	url_t url_parsed[1];
	host_t host_parsed[1];
	char domain_tmp[URL_DOMAIN_MAX];
	char host_tmp[URL_HOST_MAX];
	int rc = url_parse(URL, url_parsed);
	if(rc < 0) return rc;
	rc = host_parse(url_parsed->host, host_parsed);
	if(rc < 0) return rc;
	rc = domain_reverse(host_parsed->domain, domain_tmp, sizeof(domain_tmp));
	if(rc < 0) return rc;
	memcpy(host_parsed->domain, domain_tmp, URL_DOMAIN_MAX);
	rc = host_format(host_parsed, host_tmp, sizeof(host_tmp));
	if(rc < 0) return rc;
	memcpy(url_parsed->host, host_tmp, URL_HOST_MAX);
	rc = url_format(url_parsed, out, max);
	if(rc < 0) return rc;
	return 0;
}

