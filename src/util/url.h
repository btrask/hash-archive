// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#define PROTOCOL_MAX (31+1)
#define PROTOCOL_FMT "%31[^:]"
#define HOST_MAX (255+1)
#define HOST_FMT "%255[^/]"
#define DOMAIN_MAX (255+1)
#define DOMAIN_FMT "%255[^a-zA-Z0-9.-]"
#define PATH_MAX (1023+1)
#define PATH_FMT "%1023[^#]"

typedef struct {
	char protocol[PROTOCOL_MAX]; // Not including trailing colon
	char host[HOST_MAX]; // Includes port
	char path[PATH_MAX]; // Includes query
	// Fragment is stripped
} url_t;
int url_parse(char const *const URL, url_t *const out);
int url_format(url_t const *const URL, char *const out, size_t const max);

typedef struct {
	char domain[DOMAIN_MAX];
	unsigned int port;
} host_t;
int host_parse(char const *const host, host_t *const out);
int host_format(host_t const *const host, char *const out, size_t const max);

// https://github.com/internetarchive/surt
// http://crawler.archive.org/articles/user_manual/glossary.html#surt
int url_normalize_surt(char const *const URL, char *const out, size_t const max);

