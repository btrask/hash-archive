// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include "util/hash.h"
#include "util/html.h"
#include "page.h"

static TemplateRef header = NULL;
static TemplateRef footer = NULL;
static TemplateRef entry = NULL;
static TemplateRef notfound = NULL;
static TemplateRef short_hash = NULL;
static TemplateRef weak_hash = NULL;

int page_sources(HTTPConnectionRef const conn, strarg_t const URI) {
	HTTPConnectionSendStatus(conn, 200);
	return 0;
}

