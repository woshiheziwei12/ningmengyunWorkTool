#pragma once
#include <io.h>
#include <stdint.h>
#include <malloc.h>
#include <intrin.h>
typedef intptr_t ssize_t;
#define fseeko _fseeki64
#define ftello _ftelli64
#define alloca _alloca
#define __builtin_bswap32 _byteswap_ulong
#define TLS __declspec(thread)
#define strtok_r strtok_s
