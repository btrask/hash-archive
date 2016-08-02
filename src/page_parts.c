// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdlib.h>
#include "util/hash.h"
#include "util/html.h"
#include "util/strext.h"
#include "page.h"

char *link_html(hash_uri_type const t, strarg_t const URI_unsafe) {
	char *r = NULL;
	char *escaped = html_encode(URI_unsafe);
	if(!escaped) return NULL;
	switch(t) {
	case LINK_NONE: r = aasprintf(
		"<span>%s</span>", escaped); break;
	case LINK_RAW: r = aasprintf(
		"<a href=\"%s\">%s</a>", escaped, escaped); break;
	case LINK_WEB_URL: r = aasprintf(
		"<a href=\"/history/%s\">%s</a>"
		"<sup>[<a href=\"%s\" rel=\"nofollow\" "
			"target=\"_blank\">^</a>]</sup>",
		escaped, escaped, escaped); break;
	case LINK_HASH_URI:
	case LINK_NAMED_INFO:
	case LINK_MAGNET: r = aasprintf(
		"<a href=\"/sources/%s\">%s</a>"
		"<sup>[<a href=\"%s\" rel=\"nofollow\" "
			"target=\"_blank\">#</a>]</sup>",
		escaped, escaped, escaped); break;
	case LINK_MULTIHASH:
	case LINK_PREFIX:
	case LINK_SSB: r = aasprintf(
		"<a href=\"/sources/%s\">%s</a>", escaped, escaped); break;
	default: assertf(0, "Unknown link type %d", type);
	}
	free(escaped); escaped = NULL;
	return r;
}
char *item_html(hash_uri_type const type, strarg_t const label_escaped, strarg_t const URI_unsafe, bool const deprecated) {
	strarg_t const class = deprecated ? "deprecated" : "";
	char *link = link_html(type, URI_unsafe);
	if(!link) return NULL;
	char *r = aasprintf("<li class=\"break %s\">%s%s</li>",
		class, label_escaped, link);
	free(link); link = NULL;
	return r;
}
char *direct_link_html(hash_uri_type const type, strarg_t const URI_unsafe) {
	char *r = NULL;
	char *escaped = html_encode(URI_unsafe);
	if(!escaped) return NULL;
	switch(type) {
	case LINK_WEB_URL:
	case LINK_HASH_URI:
	case LINK_NAMED_INFO:
	case LINK_MAGNET: r = aasprintf(
		"<a href=\"%s\" rel=\"nofollow\">%s</a>",
		escaped, escaped); break;
	case LINK_MULTIHASH:
	case LINK_PREFIX:
	case LINK_SSB: r = aasprintf("<span>%s</span>", escaped); break;;
	default: assertf(0, "Unknown link type %d", type);
	}
	free(escaped); escaped = NULL;
	return r;
}

