

#ifndef COMMON_H
#define COMMON_H

#include "stdlib.h"
#include "string.h"

#define RNN_INLINE inline
#define OPUS_INLINE inline

/* VoiceFrame Pro: MSVC compatibility (no C99 variable-length arrays, no M_PI). */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#if defined(_MSC_VER)
#include <malloc.h>
#define RNN_VLA(type, name, n) type *name = (type *)_alloca(sizeof(type) * (size_t)(n))
#else
#define RNN_VLA(type, name, n) type name[n]
#endif


/** RNNoise wrapper for malloc(). To do your own dynamic allocation, all you need t
o do is replace this function and rnnoise_free */
#ifndef OVERRIDE_RNNOISE_ALLOC
static RNN_INLINE void *rnnoise_alloc (size_t size)
{
   return malloc(size);
}
#endif

/** RNNoise wrapper for free(). To do your own dynamic allocation, all you need to do is replace this function and rnnoise_alloc */
#ifndef OVERRIDE_RNNOISE_FREE
static RNN_INLINE void rnnoise_free (void *ptr)
{
   free(ptr);
}
#endif

/** Copy n elements from src to dst. The 0* term provides compile-time type checking  */
#ifndef OVERRIDE_RNN_COPY
#define RNN_COPY(dst, src, n) (memcpy((dst), (src), (n)*sizeof(*(dst)) + 0*((dst)-(src)) ))
#endif

/** Copy n elements from src to dst, allowing overlapping regions. The 0* term
    provides compile-time type checking */
#ifndef OVERRIDE_RNN_MOVE
#define RNN_MOVE(dst, src, n) (memmove((dst), (src), (n)*sizeof(*(dst)) + 0*((dst)-(src)) ))
#endif

/** Set n elements of dst to zero */
#ifndef OVERRIDE_RNN_CLEAR
#define RNN_CLEAR(dst, n) (memset((dst), 0, (n)*sizeof(*(dst))))
#endif



#endif
