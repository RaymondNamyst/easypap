#ifndef EZV_EVENT_H
#define EZV_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL3/SDL.h>

struct ezv_ctx_s;
typedef struct ezv_ctx_s *ezv_ctx_t;

int ezv_perform_1D_picking (ezv_ctx_t ctx[], unsigned nb_ctx);
void ezv_perform_2D_picking (ezv_ctx_t ctx[], unsigned nb_ctx, int *x, int *y);

int ezv_get_event (SDL_Event *event, int blocking);
void ezv_process_event (ezv_ctx_t ctx[], unsigned nb_ctx, SDL_Event *event,
                        int *refresh, int *pick);

int ezv_ctx_is_in_focus (ezv_ctx_t ctx);

#ifdef __cplusplus
}
#endif

#endif
