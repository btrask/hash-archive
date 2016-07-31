// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <async/async.h>
#include <async/http/HTTPServer.h>

typedef struct Template* TemplateRef;

typedef struct {
	char *(*lookup)(void const *const ctx, char const *const var);
	void (*free)(void const *const ctx, char const *const var, char **const val);
} TemplateArgCBs;

typedef int (*TemplateWritev)(void *, uv_buf_t[], unsigned int);

int TemplateCreate(char const *const str, TemplateRef *const out);
int TemplateCreateFromPath(char const *const path, TemplateRef *const out);
void TemplateFree(TemplateRef *const tptr);
int TemplateWrite(TemplateRef const t, TemplateArgCBs const *const cbs, void const *const actx, TemplateWritev const writev, void *wctx);
int TemplateWriteHTTPChunk(TemplateRef const t, TemplateArgCBs const *const cbs, void const *actx, HTTPConnectionRef const conn);
int TemplateWriteFile(TemplateRef const t, TemplateArgCBs const *const cbs, void const *actx, uv_file const file);

typedef struct {
	char const *var;
	char const *val;
} TemplateStaticArg;
extern TemplateArgCBs const TemplateStaticCBs;

