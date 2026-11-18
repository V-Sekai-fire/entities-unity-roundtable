#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(_WIN32)
#define RT_API __declspec(dllexport)
#else
#define RT_API __attribute__((visibility("default")))
#endif
typedef int32_t (*rt_get_fn)(int32_t id, int32_t axis); /* float bits */
typedef void (*rt_set_fn)(int32_t id, int32_t axis, int32_t bits);
enum { RT_WAMR = 0, RT_RISCV = 1 };
/* The host API both VMs expose to guests as host_get / host_set. */
RT_API void rt_set_host(rt_get_fn get, rt_set_fn set);
/* Load + instantiate a module (wasm for RT_WAMR, ELF for RT_RISCV). Returns a handle > 0, or 0 and rt_error(). */
RT_API int32_t rt_load(int32_t vm, const uint8_t *bytes, uint32_t len);
RT_API void rt_unload(int32_t handle);
/* Call export `fn` with up to one i32 argument; returns its i64 result (i32 results sign-extended); *ok = 0 on a fault. */
RT_API int64_t rt_call(int32_t handle, const char *fn, int32_t arg, int32_t *ok);
/* Call `fn(arg)` `count` times in native code (no per-call P/Invoke); returns the elapsed nanoseconds. */
RT_API int64_t rt_call_n(int32_t handle, const char *fn, int32_t arg, int32_t count, int32_t *ok);
RT_API const char *rt_error(void);
#ifdef __cplusplus
}
#endif
