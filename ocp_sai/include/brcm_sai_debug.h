#ifndef _BRCM_SAI_DEBUG_
#define _BRCM_SAI_DEBUG_

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern void
_brcm_sai_assert_memory_untracked(uintptr_t ptr, uintptr_t cookie);

extern void
_brcm_sai_assert_memory_tracked(uintptr_t ptr, uintptr_t cookie);

extern void
_brcm_sai_trace_alloc_memory(uintptr_t ptr, uintptr_t cookie);

extern void
_brcm_sai_trace_free_memory(uintptr_t ptr, uintptr_t cookie);

extern void
brcm_sai_mem_debug_init();

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // _BRCM_SAI_DEBUG_

