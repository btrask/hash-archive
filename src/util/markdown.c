// Copyright 2014-2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <regex.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <async/http/QueryString.h>
#include <buffer.h> // cmark
#include "strext.h"
#include "markdown.h"

// TODO
#define HASH_INFO_MSG "Hash URI (right click and choose copy link)"

// HACK
extern cmark_mem DEFAULT_MEM_ALLOCATOR;

// TODO
#define STR_LEN(x) (x), (sizeof(x)-1)

// Ported to the JS version in markdown.js
// The output should be identical between each version
// TODO: Error handling

static cmark_node *superscript(char const *const label, char const *const title, char const *const link) {
	cmark_node *node = cmark_node_new(CMARK_NODE_CUSTOM_INLINE);

	cmark_node *a = cmark_node_new(CMARK_NODE_LINK);
	cmark_node_set_url(a, link);
	cmark_node_set_title(a, title);

	cmark_node *op = cmark_node_new(CMARK_NODE_INLINE_HTML);
	cmark_node_set_literal(op, "<sup>[");
	cmark_node *ed = cmark_node_new(CMARK_NODE_INLINE_HTML);
	cmark_node_set_literal(ed, "]</sup>");

	cmark_node *face = cmark_node_new(CMARK_NODE_TEXT);
	cmark_node_set_literal(face, label);
	cmark_node_append_child(a, face);

	cmark_node_append_child(node, op);
	cmark_node_append_child(node, a);
	cmark_node_append_child(node, ed);

	return node;
}

static void md_escape(cmark_iter *const iter) {
	for(;;) {
		cmark_event_type const event = cmark_iter_next(iter);
		if(CMARK_EVENT_DONE == event) break;
		if(CMARK_EVENT_ENTER != event) continue;
		cmark_node *const node = cmark_iter_get_node(iter);
		if(CMARK_NODE_HTML != cmark_node_get_type(node)) continue;

		char const *const str = cmark_node_get_literal(node);
		cmark_node *p = cmark_node_new(CMARK_NODE_PARAGRAPH);
		cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
		cmark_node_set_literal(text, str);
		cmark_node_append_child(p, text);
		cmark_node_insert_before(node, p);
		cmark_node_free(node);
	}
}
static void md_escape_inline(cmark_iter *const iter) {
	for(;;) {
		cmark_event_type const event = cmark_iter_next(iter);
		if(CMARK_EVENT_DONE == event) break;
		if(CMARK_EVENT_ENTER != event) continue;
		cmark_node *const node = cmark_iter_get_node(iter);
		if(CMARK_NODE_INLINE_HTML != cmark_node_get_type(node)) continue;

		char const *const str = cmark_node_get_literal(node);
		cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
		cmark_node_set_literal(text, str);
		cmark_node_insert_before(node, text);
		cmark_node_free(node);
	}
}
static void md_autolink(cmark_iter *const iter) {
	regex_t linkify[1];
	// <http://daringfireball.net/2010/07/improved_regex_for_matching_urls>
	// Painstakingly ported to POSIX
	int rc = regcomp(linkify, "([a-z][a-z0-9_-]+:(/{1,3}|[a-z0-9%])|www[0-9]{0,3}[.]|[a-z0-9.-]+[.][a-z]{2,4}/)([^[:space:]()<>]+|\\(([^[:space:]()<>]+|(\\([^[:space:]()<>]+\\)))*\\))+(\\(([^[:space:]()<>]+|(\\([^[:space:]()<>]+\\)))*\\)|[^][[:space:]`!(){};:'\".,<>?«»“”‘’])", REG_ICASE | REG_EXTENDED);
	assert(0 == rc);

	for(;;) {
		cmark_event_type const event = cmark_iter_next(iter);
		if(CMARK_EVENT_DONE == event) break;
		if(CMARK_EVENT_ENTER != event) continue;
		cmark_node *const node = cmark_iter_get_node(iter);
		if(CMARK_NODE_TEXT != cmark_node_get_type(node)) continue;

		char const *const str = cmark_node_get_literal(node);
		char const *pos = str;
		regmatch_t match;
		while(0 == regexec(linkify, pos, 1, &match, 0)) {
			regoff_t const loc = match.rm_so;
			regoff_t const len = match.rm_eo - match.rm_so;

			char *pfx = strndup(pos, loc);
			char *link_abs = strndup(pos+loc, len);
			char *link_rel = aasprintf("/history/%s", link_abs);
			assert(pfx);
			assert(link_abs);
			assert(link_rel);

			cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
			cmark_node_set_literal(text, pfx);
			cmark_node *link = cmark_node_new(CMARK_NODE_LINK);
			cmark_node_set_url(link, link_rel);
			cmark_node *sup = superscript("^", "", link_abs);
			cmark_node *face = cmark_node_new(CMARK_NODE_TEXT);
			cmark_node_set_literal(face, link_abs);
			cmark_node_append_child(link, face);
			cmark_node_insert_before(node, text);
			cmark_node_insert_before(node, link);
			cmark_node_insert_before(node, sup);

			free(pfx); pfx = NULL;
			free(link_abs); link_abs = NULL;
			free(link_rel); link_rel = NULL;

			pos += loc+len;
		}

		if(str != pos) {
			cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
			cmark_node_set_literal(text, pos);
			cmark_node_insert_before(node, text);
			cmark_node_free(node);
		}

	}
	regfree(linkify);
}
static void md_block_external_images(cmark_iter *const iter) {
	for(;;) {
		cmark_event_type const event = cmark_iter_next(iter);
		if(CMARK_EVENT_DONE == event) break;
		if(CMARK_EVENT_EXIT != event) continue;
		cmark_node *const node = cmark_iter_get_node(iter);
		if(CMARK_NODE_IMAGE != cmark_node_get_type(node)) continue;

		char const *const URI = cmark_node_get_url(node);
		if(URI) {
			if(0 == strncasecmp(URI, STR_LEN("hash:"))) continue;
			if(0 == strncasecmp(URI, STR_LEN("data:"))) continue;
		}

		cmark_node *link = cmark_node_new(CMARK_NODE_LINK);
		cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
		cmark_node_set_url(link, URI);
		for(;;) {
			cmark_node *child = cmark_node_first_child(node);
			if(!child) break;
			cmark_node_append_child(link, child);
		}
		if(cmark_node_first_child(link)) {
			cmark_node_set_literal(text, " (external image)");
		} else {
			cmark_node_set_literal(text, "(external image)");
		}
		cmark_node_append_child(link, text);

		cmark_node_insert_before(node, link);
		cmark_node_free(node);
	}
}
static void md_convert_hashes(cmark_iter *const iter) {
	for(;;) {
		cmark_event_type const event = cmark_iter_next(iter);
		if(CMARK_EVENT_DONE == event) break;
		if(CMARK_EVENT_EXIT != event) continue;
		cmark_node *const node = cmark_iter_get_node(iter);
		cmark_node_type const type = cmark_node_get_type(node);
		if(CMARK_NODE_LINK != type && CMARK_NODE_IMAGE != type) continue;

		char const *const URI = cmark_node_get_url(node);
		if(!URI) continue;
		if(0 != strncasecmp(URI, STR_LEN("hash:"))) continue;

		cmark_node *sup = superscript("#", HASH_INFO_MSG, URI);
		cmark_node_insert_after(node, sup);
		cmark_iter_reset(iter, sup, CMARK_EVENT_EXIT);

		char *escaped = QSEscape(URI, strlen(URI), true);
		size_t const elen = strlen(escaped);
		cmark_strbuf rel[1];
		char const qpfx[] = "?q=";
		cmark_strbuf_init(&DEFAULT_MEM_ALLOCATOR, rel, sizeof(qpfx)-1+elen);
		cmark_strbuf_put(rel, (unsigned char const *)qpfx, sizeof(qpfx)-1);
		cmark_strbuf_put(rel, (unsigned char const *)escaped, elen);
		free(escaped); escaped = NULL;
		cmark_node_set_url(node, cmark_strbuf_cstr(rel));
		cmark_strbuf_free(rel);
	}
}

int md_process(cmark_node *const node) {
	assert(node);
	cmark_iter *iter = cmark_iter_new(node);
	assert(iter);

	// Due to the way cmark_iter_reset works, we're missing
	// the first event each time. But that's fine.
	cmark_iter_reset(iter, node, CMARK_EVENT_ENTER);
	md_escape(iter);
	cmark_iter_reset(iter, node, CMARK_EVENT_ENTER);
	md_escape_inline(iter);
	cmark_iter_reset(iter, node, CMARK_EVENT_ENTER);
	md_autolink(iter);
	cmark_iter_reset(iter, node, CMARK_EVENT_ENTER);
	md_block_external_images(iter);
	cmark_iter_reset(iter, node, CMARK_EVENT_ENTER);
	md_convert_hashes(iter);

	cmark_iter_free(iter); iter = NULL;
	return 0;
}

