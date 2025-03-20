#ifndef EZV_SDL_GL_H
#define EZV_SDL_GL_H

#ifdef __cplusplus
extern "C" {
#endif

// Usa GLAD on all platforms except macOS
#ifndef __APPLE__
#define USE_GLAD
#endif

#ifndef USE_GLAD
#include <OpenGL/gl3.h>
// gl3.h must be included before SDL.h
#include <SDL3/SDL.h>

#else

#include <glad/glad.h>
// glad.h should be included before SDL.h
#include <SDL3/SDL.h>

#endif

struct ezv_ctx_s;
typedef struct ezv_ctx_s *ezv_ctx_t;

SDL_Window *ezv_sdl_window (ezv_ctx_t ctx);
SDL_GLContext ezv_glcontext (ezv_ctx_t ctx);

#ifdef __cplusplus
}
#endif

#endif
