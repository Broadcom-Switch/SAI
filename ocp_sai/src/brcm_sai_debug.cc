#include "brcm_sai_debug.h"
#include <unordered_map>
#include <pthread.h>

extern "C" {
    #include <assert.h>
    #include <malloc.h>
};

static bool __debug_init_done = false;

#define INIT_MODULE()                               \
        {                                           \
            if (unlikely(! __debug_init_done)) {    \
                __init_debug();                     \
            }                                       \
        }


static std::unordered_map<uintptr_t, uintptr_t> __mem_dict;
static pthread_mutex_t __mutex = PTHREAD_MUTEX_INITIALIZER;

static void
__init_debug()
{
    __mem_dict.clear();
    __debug_init_done = true;
}

static void
_brcm_sai_assert_memory_untracked_locked(uintptr_t ptr, uintptr_t cookie)
{
    auto value = __mem_dict.find(ptr);
    assert(value == __mem_dict.end());
}

static void
_brcm_sai_assert_memory_tracked_locked(uintptr_t ptr, uintptr_t cookie)
{
    auto value = __mem_dict.find(ptr);
    assert(value != __mem_dict.end());
}

void
_brcm_sai_assert_memory_tracked(uintptr_t ptr, uintptr_t cookie)
{
    pthread_mutex_lock(&__mutex);
    _brcm_sai_assert_memory_tracked_locked(ptr, cookie);
    pthread_mutex_unlock(&__mutex);
}

void
_brcm_sai_trace_alloc_memory(uintptr_t ptr, uintptr_t cookie)
{
    pthread_mutex_lock(&__mutex);
    __mem_dict[ptr] = cookie;
    pthread_mutex_unlock(&__mutex);
}

void
_brcm_sai_trace_free_memory(uintptr_t ptr, uintptr_t cookie)
{
    pthread_mutex_lock(&__mutex);
    _brcm_sai_assert_memory_tracked_locked(ptr, cookie);
    __mem_dict.erase(ptr);
    pthread_mutex_unlock(&__mutex);
}

void
brcm_sai_mem_debug_init()
{
    INIT_MODULE();
#if 1
    // Extreme memory debug code. Has lots of overhead but great for
    // use-after-free.
    mallopt(M_MMAP_THRESHOLD, 0);
#endif
}


