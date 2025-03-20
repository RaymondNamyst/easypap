//#include <cglm/cglm.h>
#include <stdio.h>
#include <stdlib.h>

#include "error.h"
#include "ezv_ctx.h"
#include "ezv_hud.h"
#include "ezv_prefix.h"
#include "ezv_sdl_gl.h"
#include "ezv_shader.h"
#include "ezv_virtual.h"

#include "ezv_img2d_object.h"
#include "ezv_mesh3d_object.h"
#include "ezv_mon_object.h"

#include "mesh3d_renderer.h"

#ifndef INSTALL_DIR
#define INSTALL_DIR "."
#endif

static unsigned MAX_WINDOW_HEIGHT = 0;

void _ezv_init (const char *prefix)
{
  static int done = 0;

  if (!done) {
    strcpy (ezv_prefix, prefix ? prefix : INSTALL_DIR);

    if (!SDL_WasInit (SDL_INIT_VIDEO)) {
      int r = SDL_Init (SDL_INIT_VIDEO);
      if (r < 0)
        exit_with_error ("Video initialization failed: %s", SDL_GetError ());
    }

    int num_displays;
    
    SDL_Rect display_bounds;

    SDL_DisplayID *display_ids = SDL_GetDisplays (&num_displays);

    if (!SDL_GetDisplayBounds(display_ids[0], &display_bounds))
        exit_with_error ("SDL_GetDisplayBounds failed: %s\n", SDL_GetError());
    SDL_free(display_ids);

    int screen_height = display_bounds.h;

    MAX_WINDOW_HEIGHT = screen_height - 128; // to account for headers, footers, etc.

    done = 1;
  }
}

void ezv_load_opengl (void)
{
  static int done = 0;
  if (!done) {
#ifdef USE_GLAD
    if (!gladLoadGLLoader ((GLADloadproc)SDL_GL_GetProcAddress))
      exit_with_error ("Failed to initialize GLAD");
    fprintf (stderr, "OpenGL dynamically loaded, version %s\n",
             glGetString (GL_VERSION));
#else
    fprintf (stderr, "OpenGL loaded, GLSL version = %s\n",
             glGetString (GL_SHADING_LANGUAGE_VERSION));
#endif
    done = 1;
  }
}

unsigned ezv_get_max_display_height (void)
{
  return MAX_WINDOW_HEIGHT;
}

ezv_ctx_t ezv_ctx_create (ezv_ctx_type_t ctx_type, const char *win_title, int x,
                          int y, int w, int h, int flags)
{
  ezv_ctx_t ctx     = NULL;

  ctx = (ezv_ctx_t)malloc (sizeof (struct ezv_ctx_s));

  ctx->type       = ctx_type;
  ctx->winw       = w;
  ctx->winh       = h;
  ctx->cpu_colors = NULL;
  ezv_palette_init (&ctx->cpu_palette);
  ezv_palette_init (&ctx->data_palette);
  ctx->hud_ctx         = NULL;
  ctx->picking_enabled = (flags & EZV_ENABLE_PICKING) ? 1 : 0;
  ctx->hud_enabled     = (flags & EZV_ENABLE_HUD) ? 1 : 0;
  ctx->clipping_enabled =
      ((flags & EZV_ENABLE_CLIPPING) && (ctx_type != EZV_CTX_TYPE_IMG2D)) ? 1
                                                                          : 0;
  ctx->screenshot_enabled = (flags & EZV_ENABLE_SCREENSHOT) ? 1 : 0;
  ctx->clipping_active    = 0;
  ctx->object             = NULL;
  ctx->class              = NULL;

  SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK,
                       SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
  SDL_GL_SetAttribute ((SDL_GLAttr)SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG, 1);
#endif

  uint32_t sdl_flags = SDL_WINDOW_OPENGL;
  if (flags & EZV_HIDE_WINDOW)
    sdl_flags |= SDL_WINDOW_HIDDEN;

  ctx->win = SDL_CreateWindow (win_title, w, h, sdl_flags);
  SDL_RaiseWindow (ctx->win);

  ctx->windowID = SDL_GetWindowID (ctx->win);

  ctx->glcontext = SDL_GL_CreateContext (ctx->win);
  if (ctx->glcontext == NULL)
    exit_with_error ("SDL_GL_CreateContext failed (%s)", SDL_GetError ());

  // Just in case it is not already loaded
  ezv_load_opengl ();

  if (flags & EZV_ENABLE_VSYNC) {
    int r = SDL_GL_SetSwapInterval (1);
    if (r != 0)
      fprintf (stderr,
               "WARNING: SDL_GL_SetSwapInterval is not supported by your "
               "OpenGL driver (%s)",
               (char *)glGetString (GL_RENDERER));
  }

  switch (ctx_type) {
  case EZV_CTX_TYPE_IMG2D: {
    // Initialize img2d specific data
    ezv_img2d_object_init (ctx);
    break;
  }
  case EZV_CTX_TYPE_MESH3D: {
    // Initialize mesh3d specific data
    ezv_mesh3d_object_init (ctx);
    break;
  }
  case EZV_CTX_TYPE_MONITOR: {
    // Initialize mon specific data
    ezv_mon_object_init (ctx);
    break;
  }
  default:
    exit_with_error ("ctx_type %d not supported", ctx_type);
  }

  // Initialize hud engine
  if (ctx->hud_enabled)
    ezv_hud_init (ctx);

  return ctx;
}

ezv_ctx_type_t ezv_ctx_type (ezv_ctx_t ctx)
{
  return ctx->type;
}

char *ezv_ctx_typestr (ezv_ctx_t ctx)
{
  switch (ctx->type) {
  case EZV_CTX_TYPE_IMG2D:
    return "IMG2D";
  case EZV_CTX_TYPE_MESH3D:
    return "MESH3D";
  case EZV_CTX_TYPE_MONITOR:
    return "MONITOR";
  default:
    return "UNDEFINED";
  }
}

unsigned ezv_ctx_width (ezv_ctx_t ctx)
{
  return ctx->winw;
}

unsigned ezv_ctx_height (ezv_ctx_t ctx)
{
  return ctx->winh;
}

void ezv_ctx_raise (ezv_ctx_t ctx)
{
  SDL_RaiseWindow (ezv_sdl_window (ctx));
}

void ezv_ctx_show (ezv_ctx_t ctx)
{
  SDL_ShowWindow (ezv_sdl_window (ctx));
}

void ezv_ctx_hide (ezv_ctx_t ctx)
{
  SDL_HideWindow (ezv_sdl_window (ctx));
}

void ezv_switch_to_context (ezv_ctx_t ctx)
{
  SDL_GL_MakeCurrent (ctx->win, ctx->glcontext);
}

void ezv_ctx_destroy (ezv_ctx_t ctx)
{
  if (ctx->object != NULL)
    free (ctx->object);
  if (ctx->hud_ctx != NULL)
    free (ctx->hud_ctx);
  free (ctx);
}

void ezv_reset_cpu_colors (ezv_ctx_t ctx)
{
  if (!ezv_palette_is_defined (&ctx->cpu_palette))
    exit_with_error ("CPU palette not initialized");

  memset (ctx->cpu_colors, 0,
          ezv_get_color_data_size (ctx) * sizeof (unsigned));
}

static void alloc_cpu_colors (ezv_ctx_t ctx)
{
  ctx->cpu_colors = malloc (ezv_get_color_data_size (ctx) * sizeof (unsigned));
  ezv_reset_cpu_colors (ctx);
}

void ezv_set_cpu_color_1D (ezv_ctx_t ctx, unsigned offset, unsigned size,
                           uint32_t color)
{
  if (!ezv_palette_is_defined (&ctx->cpu_palette))
    exit_with_error ("CPU palette not initialized");

  if (ezv_c2a (color) == 255) {
    // Don't waste time with unnecessary blend computations
    for (unsigned c = offset; c < offset + size; c++)
      ctx->cpu_colors[c] = color;
  } else {
#if 0
    vec4 src = {(float)ezv_c2r (color), (float)ezv_c2g (color),
                (float)ezv_c2b (color), 255.0};
    vec4 tmp;
    float srcA = (float)ezv_c2a (color) * (1.0f / 255.0f); // srcA in [0.0..1.0]
    for (unsigned c = offset; c < offset + size; c++) {
      // Blending formula:
      //   dstA = 1 * srcA + dstA * (1 - srcA)
      //   dstRGB = srcRGB * srcA + dstRGB * (1 - srcA)
      uint32_t d = ctx->cpu_colors[c];
      vec4 dst   = {(float)ezv_c2r (d), (float)ezv_c2g (d), (float)ezv_c2b (d),
                    (float)ezv_c2a (d)};
      glm_vec4_mix (dst, src, srcA, tmp);
      ctx->cpu_colors[c] = ezv_rgba ((uint8_t)tmp[0], (uint8_t)tmp[1],
                                     (uint8_t)tmp[2], (u_int8_t)tmp[3]);
    }
#else
    unsigned int alpha     = ezv_c2a (color) + 1;
    unsigned int inv_alpha = 256 - alpha + 1;
    color |= ezv_alpha_mask ();
    for (unsigned c = offset; c < offset + size; c++) {
      uint32_t d = ctx->cpu_colors[c];
      uint8_t r, g, b, a;
      r = (unsigned char)((alpha * ezv_c2r (color) + inv_alpha * ezv_c2r (d)) >>
                          8);
      g = (unsigned char)((alpha * ezv_c2g (color) + inv_alpha * ezv_c2g (d)) >>
                          8);
      b = (unsigned char)((alpha * ezv_c2b (color) + inv_alpha * ezv_c2b (d)) >>
                          8);
      a = (unsigned char)((alpha * ezv_c2a (color) + inv_alpha * ezv_c2a (d)) >>
                          8);
      ctx->cpu_colors[c] = ezv_rgba (r, g, b, a);
    }
#endif
  }
}

void ezv_set_cpu_color_2D (ezv_ctx_t ctx, unsigned x, unsigned width,
                           unsigned y, unsigned height, uint32_t color)
{
  if (!ezv_palette_is_defined (&ctx->cpu_palette))
    exit_with_error ("CPU palette not initialized");

  unsigned pitch = ezv_get_linepitch (ctx);
  if (ezv_c2a (color) == 255) {
    // Don't waste time with unnecessary blend computations
    for (unsigned i = y; i < y + height; i++)
      for (unsigned j = x; j < x + width; j++)
        ctx->cpu_colors[i * pitch + j] = color;
  } else {
#if 0
    vec4 src = {(float)ezv_c2r (color), (float)ezv_c2g (color),
                (float)ezv_c2b (color), 255.0};
    vec4 tmp;
    float srcA = (float)ezv_c2a (color) * (1.0f / 255.0f); // srcA in [0.0..1.0]
    for (unsigned i = y; i < y + height; i++)
      for (unsigned j = x; j < x + width; j++) {
        // Blending formula:
        //   dstA = 1 * srcA + dstA * (1 - srcA)
        //   dstRGB = srcRGB * srcA + dstRGB * (1 - srcA)
        uint32_t d = ctx->cpu_colors[i * pitch + j];
        vec4 dst = {(float)ezv_c2r (d), (float)ezv_c2g (d), (float)ezv_c2b (d),
                    (float)ezv_c2a (d)};
        glm_vec4_mix (dst, src, srcA, tmp);
        ctx->cpu_colors[i * pitch + j] =
            ezv_rgba ((uint8_t)tmp[0], (uint8_t)tmp[1], (uint8_t)tmp[2],
                      (u_int8_t)tmp[3]);
      }
#else
    unsigned int alpha     = ezv_c2a (color) + 1;
    unsigned int inv_alpha = 256 - alpha + 1;
    color |= ezv_alpha_mask ();
    for (unsigned i = y; i < y + height; i++)
      for (unsigned j = x; j < x + width; j++) {
        uint32_t d = ctx->cpu_colors[i * pitch + j];
        uint8_t r, g, b, a;
        r = (unsigned char)((alpha * ezv_c2r (color) +
                             inv_alpha * ezv_c2r (d)) >>
                            8);
        g = (unsigned char)((alpha * ezv_c2g (color) +
                             inv_alpha * ezv_c2g (d)) >>
                            8);
        b = (unsigned char)((alpha * ezv_c2b (color) +
                             inv_alpha * ezv_c2b (d)) >>
                            8);
        a = (unsigned char)((alpha * ezv_c2a (color) +
                             inv_alpha * ezv_c2a (d)) >>
                            8);
        ctx->cpu_colors[i * pitch + j] = ezv_rgba (r, g, b, a);
      }
#endif
  }
}

void ezv_use_cpu_colors (ezv_ctx_t ctx)
{
  ezv_palette_set_RGBA_passthrough (&ctx->cpu_palette);
  alloc_cpu_colors (ctx);
  ezv_activate_rgba_palette (ctx);
}

void ezv_use_data_colors_predefined (ezv_ctx_t ctx, ezv_palette_name_t name)
{
  ezv_palette_set_predefined (&ctx->data_palette, name);
  ezv_activate_data_palette (ctx);
}

void ezv_use_data_colors (ezv_ctx_t ctx, float *data, unsigned size)
{
  if (size < 2)
    exit_with_error (
        "Size (%d) too small: at least two colors must be provided", size);

  ezv_palette_set_raw (&ctx->data_palette, data, size);
  ezv_activate_data_palette (ctx);
}

void ezv_toggle_clipping (ezv_ctx_t ctx[], unsigned nb_ctx)
{
  for (int i = 0; i < nb_ctx; i++)
    if (ctx[i]->clipping_enabled)
      ctx[i]->clipping_active ^= 1;

  ezv_move_zplane (ctx, nb_ctx, 0.0f);
}

void ezv_helper_ctx_next_coord (ezv_ctx_t ctx[], unsigned ctx_no,
                                unsigned *xwin, unsigned *ywin)
{
  if (ctx_no == 0) {
    *xwin = 0;
    *ywin = 0;
    return;
  }

  // ctx_no > 0
  SDL_Window *win = ezv_sdl_window (ctx[ctx_no - 1]);
  int x = 0, y = 0, w = 0;

  SDL_GetWindowPosition (win, &x, &y);
  SDL_GetWindowSize (win, &w, NULL);

  *xwin = x + w;
  *ywin = y;
}
