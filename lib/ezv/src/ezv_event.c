#include <SDL3/SDL_events.h>

#include "ezv_event.h"
#include "error.h"
#include "ezv_ctx.h"
#include "ezv_virtual.h"

static int mouse[2]      = {-1, -1};
static int active_winID  = -1;
static int mouse_click_x = -1;
static int mouse_click_y = -1;
static int button_down   = 0;

static void mouse_enter (ezv_ctx_t ctx[], unsigned nb_ctx, SDL_Event *event)
{
  for (int c = 0; c < nb_ctx; c++)
    if (event->window.windowID == ctx[c]->windowID) {
      active_winID = ctx[c]->windowID;
      return;
    }
}

static void mouse_leave (ezv_ctx_t ctx[], unsigned nb_ctx, SDL_Event *event)
{
  mouse[0]     = -1;
  mouse[1]     = -1;
  active_winID = -1;
}

static void mouse_focus (ezv_ctx_t ctx[], unsigned nb_ctx, SDL_Event *event)
{
  mouse[0] = event->motion.x;
  mouse[1] = event->motion.y;
}

int ezv_ctx_is_in_focus (ezv_ctx_t ctx)
{
  return active_winID == ctx->windowID;
}

static int active_ctx_enables_picking (ezv_ctx_t ctx[], unsigned nb_ctx)
{
  for (int c = 0; c < nb_ctx; c++)
    if (ezv_ctx_is_in_focus (ctx[c]))
      return ctx[c]->picking_enabled;

  return 0;
}

int ezv_perform_1D_picking (ezv_ctx_t ctx[], unsigned nb_ctx)
{
  if (active_winID == -1)
    return -1;

  for (int c = 0; c < nb_ctx; c++)
    if (ctx[c]->picking_enabled && ezv_ctx_is_in_focus (ctx[c]))
      return ezv_do_1D_picking (ctx[c], mouse[0], mouse[1]);

  return -1;
}

void ezv_perform_2D_picking (ezv_ctx_t ctx[], unsigned nb_ctx, int *x, int *y)
{
  if (active_winID != -1) {
    for (int c = 0; c < nb_ctx; c++)
      if (ctx[c]->picking_enabled && ezv_ctx_is_in_focus (ctx[c])) {
        ezv_do_2D_picking (ctx[c], mouse[0], mouse[1], x, y);
        return;
      }
  }

  *x = -1;
  *y = -1;
  return;
}

// pick is set to 1 if picking should be re-done, 0 otherwise
// refresh is set to 1 if rendering should be performed
void ezv_process_event (ezv_ctx_t ctx[], unsigned nb_ctx, SDL_Event *event,
                        int *refresh, int *pick)
{
  int do_pick    = 0;
  int do_refresh = 0;

  switch (event->type) {
  case SDL_EVENT_KEY_DOWN:
    switch (event->key.key) {
    case SDLK_ESCAPE:
    case SDLK_Q:
      // Note: usually handled ahead of this function call
      exit (0);
      break;
    case SDLK_R:
      // Reset view
      ezv_reset_view (ctx, nb_ctx);
      do_pick    = active_ctx_enables_picking (ctx, nb_ctx);
      do_refresh = 1;
      break;
    case SDLK_MINUS:
    case SDLK_KP_MINUS:
    case SDLK_M:
    case SDLK_L:
      if (ezv_zoom (ctx, nb_ctx, event->key.mod & SDL_KMOD_SHIFT, 0)) {
        do_pick    = active_ctx_enables_picking (ctx, nb_ctx);
        do_refresh = 1;
      }
      break;
    case SDLK_PLUS:
    case SDLK_KP_PLUS:
    case SDLK_EQUALS:
    case SDLK_P:
    case SDLK_O:
      if (ezv_zoom (ctx, nb_ctx, event->key.mod & SDL_KMOD_SHIFT, 1)) {
        do_pick    = active_ctx_enables_picking (ctx, nb_ctx);
        do_refresh = 1;
      }
      break;
    case SDLK_C:
      // Toggle clipping
      ezv_toggle_clipping (ctx, nb_ctx);
      do_pick    = active_ctx_enables_picking (ctx, nb_ctx);
      do_refresh = 1;
      break;
    }
    break;
  case SDL_EVENT_QUIT: // normally handled by easypap/easyview
    exit (0);
    break;
  case SDL_EVENT_MOUSE_MOTION:
    mouse_focus (ctx, nb_ctx, event);
    do_pick = active_ctx_enables_picking (ctx, nb_ctx);
    if (button_down) {
      if (ezv_motion (ctx, nb_ctx, (event->motion.x - mouse_click_x),
                      (event->motion.y - mouse_click_y), 0)) {
        do_pick    = active_ctx_enables_picking (ctx, nb_ctx);
        do_refresh = 1;
      }
      mouse_click_x = event->motion.x;
      mouse_click_y = event->motion.y;
    }
    break;
  case SDL_EVENT_MOUSE_WHEEL: {
    if (ezv_motion (ctx, nb_ctx, -event->wheel.x, event->wheel.y, 1)) {
      do_pick    = active_ctx_enables_picking (ctx, nb_ctx);
      do_refresh = 1;
    }
  } break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    mouse_click_x = event->button.x;
    mouse_click_y = event->button.y;
    button_down   = 1;
    break;
  case SDL_EVENT_MOUSE_BUTTON_UP: {
    button_down = 0;
    break;
  }
  case SDL_EVENT_WINDOW_MOUSE_ENTER: {
    mouse_enter (ctx, nb_ctx, event);
    break;
  }
  case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
    mouse_leave (ctx, nb_ctx, event);
    do_pick = active_ctx_enables_picking (ctx, nb_ctx);
    break;
  }
  }

  if (refresh)
    *refresh = do_refresh;
  if (pick)
    *pick = do_pick;
}

static int get_event (SDL_Event *event, int blocking)
{
  return blocking ? SDL_WaitEvent (event) : SDL_PollEvent (event);
}

int ezv_get_event (SDL_Event *event, int blocking)
{
  int r;
  static int prefetched = 0;
  static SDL_Event pr_event; // prefetched event

  if (prefetched) {
    *event     = pr_event;
    prefetched = 0;
    return 1;
  }

  r = get_event (event, blocking);

  if (r != 1)
    return r;

  // check if successive, similar events can be dropped
  if (event->type == SDL_EVENT_MOUSE_MOTION) {

    do {
      int ret_code = get_event (&pr_event, 0);
      if (ret_code == 1) {
        if (pr_event.type == SDL_EVENT_MOUSE_MOTION) {
          *event     = pr_event;
          prefetched = 0;
        } else {
          prefetched = 1;
        }
      } else
        return 1;
    } while (prefetched == 0);
  }

  return 1;
}
