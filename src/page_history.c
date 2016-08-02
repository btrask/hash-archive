// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include "util/hash.h"
#include "util/html.h"
#include "page.h"

static TemplateRef header = NULL;
static TemplateRef footer = NULL;
static TemplateRef entry = NULL;
static TemplateRef error = NULL;
static TemplateRef outdated = NULL;

int page_history(HTTPConnectionRef const conn, strarg_t const URL) {
	HTTPConnectionSendStatus(conn, 200);
	return 0;
}

