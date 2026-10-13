#pragma once
#include <stdint.h>
typedef intptr_t cell;
typedef struct { int unused; } AMX;
#define AMX_NATIVE_CALL
typedef struct { const char *name; cell (*function)(AMX *, const cell *); } AMX_NATIVE_INFO;
#define amx_StrParam(amx,param,result) ((result) = (char *)(uintptr_t)(param))
#define AMX_ERR_NONE 0
#define AMX_ERR_NATIVE 10
static inline int amx_Register(AMX *amx, const AMX_NATIVE_INFO *info, int n)
{ (void)amx; (void)info; (void)n; return 0; }
