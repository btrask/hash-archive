// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdbool.h>
#include <async/http/HTTP.h>
#include "util/Template.h"

#define URI_MAX (1023+1)

#define assertf(x, ...) assert(x) // TODO

// TODO
typedef char str_t;
typedef char const *strarg_t;

char *link_html(hash_uri_type const t, strarg_t const URI_unsafe);
char *item_html(hash_uri_type const type, strarg_t const label_escaped, strarg_t const URI_unsafe, bool const deprecated);
char *direct_link_html(hash_uri_type const type, strarg_t const URI_unsafe);

int page_index(HTTPConnectionRef const conn);
int page_history(HTTPConnectionRef const conn, strarg_t const URL);
int page_sources(HTTPConnectionRef const conn, strarg_t const URI);


static void template_load(strarg_t const path, TemplateRef *const out) {
	// TODO
	char full[4095+1];
	int rc = snprintf(full, sizeof(full), "/home/user/Code/hash-archive/templates/%s", path);
	assertf(rc >= 0, "Template path error: %s\n", strerror(errno));
	assert(rc < sizeof(full));
	rc = TemplateCreateFromPath(full, out);
	assertf(rc >= 0, "Template load error: %s\n", uv_strerror(rc));
}
