// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <async/async.h>
#include <async/http/HTTPServer.h>

typedef struct Template* TemplateRef;

typedef int (*TemplateWriteFn)(void *, uv_buf_t);
typedef int (*TemplateVarFn)(void *, char const *, TemplateWriteFn, void *);

int TemplateCreate(char const *const str, TemplateRef *const out);
int TemplateCreateFromPath(char const *const path, TemplateRef *const out);
void TemplateFree(TemplateRef *const tptr);
int TemplateWrite(TemplateRef const t, TemplateVarFn const var, void *const actx, TemplateWriteFn const wr, void *const wctx);
int TemplateWriteHTTPChunk(TemplateRef const t, TemplateVarFn const var, void *actx, HTTPConnectionRef const conn);
int TemplateWriteFile(TemplateRef const t, TemplateVarFn const var, void *actx, uv_file const file);

typedef struct {
	char const *var;
	char const *val;
} TemplateStaticArg;
int TemplateStaticVar(void *, char const *, TemplateWriteFn, void *);

