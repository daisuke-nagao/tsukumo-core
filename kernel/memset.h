#ifndef UUID_0195F4F1_7309_7258_9597_92815600478A
#define UUID_0195F4F1_7309_7258_9597_92815600478A

#include <tk/tkernel.h>

typedef typeof(sizeof(void *)) size_t;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern void *tkmc_memset(void *dest, int c, size_t n);
extern void *memset(void *dest, int c, size_t n);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_0195F4F1_7309_7258_9597_92815600478A */
