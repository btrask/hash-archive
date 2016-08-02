// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <stdlib.h>
#include <string.h>
#include "html.h"

/*#include "../../deps/cmark/src/houdini.h"
#include "../../deps/cmark/src/buffer.h"

// HACK
extern cmark_mem DEFAULT_MEM_ALLOCATOR;

char *html_encode(char const *const str) {
	if(!str) return NULL;
	cmark_strbuf out = CMARK_BUF_INIT(&DEFAULT_MEM_ALLOCATOR);
	houdini_escape_html(&out, (uint8_t const *)str, strlen(str));
	return (char *)cmark_strbuf_detach(&out);
}*/

// Definitions taken from cmark/houdini_html_e.c
#define ENTITIES(XX) \
	XX('&', "&amp;") \
	XX('<', "&lt;") \
	XX('>', "&gt;") \
	XX('"', "&quot;") \
	XX('\'', "&#x27;") \
	XX('/', "&#x2F;")

char *html_encode(char const *const str) {
	if(!str) return NULL;
	size_t total = 0;
	for(size_t i = 0; str[i]; ++i) switch(str[i]) {
#define X(c, s) case c: total += sizeof(s)-1; break;
		ENTITIES(X)
		default: total += 1; break;
#undef X
	}
	char *enc = malloc(total+1);
	if(!enc) return NULL;
	for(size_t i = 0, j = 0; str[i]; ++i) switch(str[i]) {
#define X(c, s) case c: memcpy(enc+j, s, sizeof(s)-1); j += sizeof(s)-1; break;
		ENTITIES(X)
#undef X
		default: enc[j++] = str[i]; break;
	}
	enc[total] = '\0';
	return enc;
}

