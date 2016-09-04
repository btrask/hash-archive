// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#ifndef COMMON_H
#define COMMON_H

#define MIN(a, b) ({ \
	__typeof__(a) const __a = (a); \
	__typeof__(b) const __b = (b); \
	__a < __b ? __a : __b; \
})
#define MAX(a, b) ({ \
	__typeof__(a) const __a = (a); \
	__typeof__(b) const __b = (b); \
	__a > __b ? __a : __b; \
})

#define STR_LEN(x) (x), (sizeof(x)-1)

#define FREE(ptrptr) do { \
	__typeof__(ptrptr) const __x = (ptrptr); \
	free(*__x); *__x = NULL; \
} while(0)

#define assertf(x, ...) assert(x) // TODO

#define URI_MAX (1023+1)
#define TYPE_MAX (255+1)

typedef char str_t;
typedef char const *strarg_t;

#endif

