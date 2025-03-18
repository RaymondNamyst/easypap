#ifndef EZP_HELPERS_H
#define EZP_HELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <omp.h>

typedef void (*void_func_t) (void);

// Call function ${kernel}_draw_${suffix}, or default_func if symbol not found
void hooks_draw_helper (char *suffix, void_func_t default_func);

// Call appropriate do_tile_${suffix} function, with calls to monitoring start/end
int do_tile_id (int x, int y, int width, int height, int who);

#define do_tile_implicit(x,y,w,h) do_tile_id(x,y,w,h,omp_get_thread_num())

// The two macros implement the switch between do_tile_implicit and do_tile_id
#define _macro(_1,_2,_3,_4,_5,NAME,...) NAME
#define do_tile(...) _macro(__VA_ARGS__,do_tile_id,do_tile_implicit)(__VA_ARGS__)

// Call appropriate do_patch_${suffix} function, with calls to monitoring start/end
int do_patch_id (int patch, int who);

#define do_patch_implicit(p) do_patch_id((p),omp_get_thread_num())

#define _macrop(_1,_2,NAME,...) NAME
#define do_patch(...) _macrop(__VA_ARGS__,do_patch_id,do_patch_implicit)(__VA_ARGS__)


#ifdef __cplusplus
}
#endif

#endif