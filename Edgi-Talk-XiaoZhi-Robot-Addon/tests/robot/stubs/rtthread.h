#pragma once
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RT_ASSERT(x) assert(x)
#define rt_kprintf printf
#define rt_malloc malloc
#define rt_free free
#define rt_realloc realloc
#define rt_memcpy memcpy
#define rt_memset memset
