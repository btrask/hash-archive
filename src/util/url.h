// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#ifndef URL_H
#define URL_H

#include <errno.h>

#define URL_SCHEME_MAX (31+1)
#define URL_SCHEME_FMT "%31[^:]"
#define URL_HOST_MAX (255+1)
#define URL_HOST_FMT "%255[^/]"
#define URL_DOMAIN_MAX (255+1)
#define URL_DOMAIN_FMT "%255[a-zA-Z0-9.-]"
#define URL_PORT_MAX (15+1)
#define URL_PORT_FMT "%15[0-9]"
#define URL_PATH_MAX (1023+1)
#define URL_PATH_FMT "%1023[^?#]"
#define URL_QUERY_MAX (1023+1)
#define URL_QUERY_FMT "%1023[^#]"

enum {
	URL_EINVAL = -EINVAL,
	URL_EPARSE = -15801,
};

typedef struct {
	char scheme[URL_SCHEME_MAX]; // Not including trailing colon
	char host[URL_HOST_MAX]; // Includes port
	char path[URL_PATH_MAX];
	char query[URL_QUERY_MAX];
	// Fragment is stripped
} url_t;
int url_parse(char const *const URL, url_t *const out);
int url_format(url_t const *const URL, char *const out, size_t const max);

typedef struct {
	char domain[URL_DOMAIN_MAX];
	char port[URL_PORT_MAX];
} host_t;
int host_parse(char const *const host, host_t *const out);
int host_format(host_t const *const host, char *const out, size_t const max);

int url_normalize(char const *const URL, char *const out, size_t const max);

// https://github.com/internetarchive/surt
// http://crawler.archive.org/articles/user_manual/glossary.html#surt
int url_normalize_surt(char const *const URL, char *const out, size_t const max);

#endif

