#ifndef ONLINE_GAUSSIAN_UTIL_H
#define ONLINE_GAUSSIAN_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <memcpy.h>

//#define DEBUG 1
#define malloc_fn(cnx, size) my_malloc(cnx, size)
#define free_fn(cnx, ptr) my_free(cnx, ptr)
#define realloc_fn(cnx, ptr, size) my_realloc(cnx, ptr, size)
#define memset_fn my_memset
#define memcpy_fn my_memcpy
#define calloc_fn(cnx, nmemb, size) my_calloc(cnx, nmemb, size)
#define memmove_fn(dst, src, size) my_memmove(dst, src, size)

#ifndef assert
#define assert(...) if(0){};
#endif

#ifndef MIN
#define MIN(a, b) (((a) <= (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) >= (b)) ? (a) : (b))
#endif

#ifdef DEBUG
#define DEBUG_PRINTF(...) do { fprintf( stderr, __VA_ARGS__ ); } while(0)
#else
#define DEBUG_PRINTF(...) do {} while(0)
#endif

/* XXX: move in more general header file */
#define WARNING_PRINT(cnx, fmt, args...) PROTOOP_PRINTF(cnx, "WARNING: %s: %d: %s(): " fmt, ##args)

#endif //ONLINE_GAUSSIAN_UTIL_H
