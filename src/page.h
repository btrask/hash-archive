// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <stdbool.h>
#include <async/http/HTTP.h>

#define URI_MAX (1023+1)

#define assertf(x, ...) assert(x) // TODO

// TODO
typedef char str_t;
typedef char const *strarg_t;

char *link_html(hash_uri_type const t, strarg_t const URI_unsafe);
char *item_html(hash_uri_type const type, strarg_t const label_escaped, strarg_t const URI_unsafe, bool const deprecated);
char *direct_link_html(hash_uri_type const type, strarg_t const URI_unsafe);

int page_index(HTTPConnectionRef const conn);

