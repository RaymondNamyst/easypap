#include <SDL3/SDL_surface.h>
#include <cglm/cglm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "error.h"
#include "ezv_textures.h"
#include "ezv_ctx.h"
#include "ezv_hud.h"
#include "ezv_sdl_gl.h"
#include "ezv_shader.h"
#include "ezv_prefix.h"
#include "stb_image.h"

#include "ezv_mon_object.h"
#include "mon_renderer.h"

typedef struct mon_render_ctx_s
{
  GLuint UBO_DATACOL;
  GLuint UBO_MAT;
  GLuint UBO_CPU;
  GLuint TBO_COL;  // Texture Buffer Object containing RGBA cpu colors
  GLuint VBO;       // Vertex Buffer Object (contains vertices)
  GLuint VAO;       // Vertex Array Object
  GLuint VBO_IND;   // Vertex Buffer Object containing triangles (i.e. 3-tuples
                    // indexing vertices)
  GLuint VAO_CPU;
  GLuint VBO_CPU;
  GLuint dataTexture;
  GLuint cpu_shader, data_shader;
  GLuint tex_color;   // TBO_COL
  GLuint cpu_colors_loc, cpu_ratios_loc;
  GLuint data_imgtex_loc;
} mon_render_ctx_t;

// Model Matrix
static struct
{
  mat4 mvp;
  mat4 ortho;
  mat4 vp_unclipped;
  mat4 mvp_unclipped;
  mat4 mv;
} Matrices;

static struct
{
  float x_offset;
  float y_offset;
  float y_stride;
} CpuInfo;

#define PERFMETER_WIDTH 256
#define MARGIN 16
#define LEGEND 64
static unsigned PERFMETER_HEIGHT = 18;
static unsigned INTERMARGIN      = 8;

void ezv_mon_get_suggested_window_size (mon_obj_t *mon, unsigned *width,
                                        unsigned *height)
{
  unsigned w, h;
  unsigned max_height = ezv_get_max_display_height ();

  for (;;) {
    w = 2 * MARGIN + LEGEND + PERFMETER_WIDTH;
    h = 2 * MARGIN + (mon->cpu + mon->gpu) * PERFMETER_HEIGHT +
        (mon->cpu + mon->gpu - 1) * INTERMARGIN;

    if (h <= max_height)
      break;

    if (INTERMARGIN > 1)
      INTERMARGIN -= 1;
    else if (PERFMETER_HEIGHT > 4)
      PERFMETER_HEIGHT -= 2;
    else
      exit_with_error ("Sorry, I'm unable to display so many CPU meters");
  }

  if (width)
    *width = w;
  if (height)
    *height = h;
}

static void mon_renderer_mvp_init (ezv_ctx_t ctx)
{
  static int done            = 0;
  mon_render_ctx_t *renctx = ezv_mon_renderer (ctx);

  // Create matrices and vector once
  if (!done) {
    glm_ortho (0.0f, (float)ctx->winw, (float)ctx->winh, 0.0f, -2.0f, 2.0f,
               Matrices.ortho);
    done = 1;
  }

  glGenBuffers (1, &renctx->UBO_MAT);
  glBindBuffer (GL_UNIFORM_BUFFER, renctx->UBO_MAT);
  glBufferData (GL_UNIFORM_BUFFER, sizeof (Matrices), &Matrices,
                GL_STATIC_DRAW);
  glBindBufferBase (GL_UNIFORM_BUFFER, BINDING_POINT_MATRICES, renctx->UBO_MAT);

  CpuInfo.x_offset = MARGIN + LEGEND;
  CpuInfo.y_offset = MARGIN;
  CpuInfo.y_stride = PERFMETER_HEIGHT + INTERMARGIN;

  glGenBuffers (1, &renctx->UBO_CPU);
  glBindBuffer (GL_UNIFORM_BUFFER, renctx->UBO_CPU);
  glBufferData (GL_UNIFORM_BUFFER, sizeof (CpuInfo), &CpuInfo,
                GL_STATIC_DRAW);
  glBindBufferBase (GL_UNIFORM_BUFFER, BINDING_POINT_CPUINFO, renctx->UBO_CPU);
}

// called by ctx_create: the mon is not defined yet, nor any palette
void mon_renderer_init (ezv_ctx_t ctx)
{
  ezv_switch_to_context (ctx);

  // configure global opengl state
  // -----------------------------
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable (GL_BLEND);

  // Allocate render_ctx
  mon_render_ctx_t *renctx = malloc (sizeof (mon_render_ctx_t));
  renctx->TBO_COL            = 0;
  renctx->UBO_DATACOL        = 0;
  renctx->UBO_MAT            = 0;
  renctx->VBO                = 0;
  renctx->VAO                = 0;
  renctx->VBO_IND            = 0;
  renctx->VAO_CPU            = 0;
  renctx->VBO_CPU            = 0;
  renctx->tex_color          = 0;
  renctx->dataTexture        = 0;

  ezv_mon_set_renderer (ctx, renctx);

  // compile shaders and build program
  renctx->cpu_shader =
      ezv_shader_create ("mon/cpu.vs", NULL, "mon/cpu.fs");
  renctx->data_shader =
      ezv_shader_create ("mon/generic.vs", NULL, "mon/generic.fs");

  // Uniform parameters
  ezv_shader_get_uniform_loc (renctx->data_shader, "dataTexture",
                              &renctx->data_imgtex_loc);

  ezv_shader_get_uniform_loc (renctx->cpu_shader, "RGBAColors",
                              &renctx->cpu_colors_loc);
  ezv_shader_get_uniform_loc (renctx->cpu_shader, "ratios",
                              &renctx->cpu_ratios_loc);

  // Bind Matrices to all shaders
  ezv_shader_bind_uniform_buf (renctx->cpu_shader, "Matrices",
                               BINDING_POINT_MATRICES);
  ezv_shader_bind_uniform_buf (renctx->data_shader, "Matrices",
                               BINDING_POINT_MATRICES);

  ezv_shader_bind_uniform_buf (renctx->cpu_shader, "CpuInfo",
                               BINDING_POINT_CPUINFO);
}

static uint32_t *ascii_data = NULL;
static SDL_Surface *ascii_surface = NULL;

static void load_ascii_surface (void)
{
  int nrChannels;
  char file[1024];
  int texture_width  = -1;
  int texture_height = -1;

  sprintf (file, "%s/share/ezv/img/ascii.png", ezv_prefix);

  ascii_data = (uint32_t *)stbi_load (file, &texture_width, &texture_height,
                                      &nrChannels, 0);
  if (ascii_data == NULL)
    exit_with_error ("Cannot open %s", file);

  ascii_surface = SDL_CreateSurfaceFrom (
      texture_width, texture_height, SDL_PIXELFORMAT_RGBA32, ascii_data, texture_width * sizeof (uint32_t));
  if (ascii_surface == NULL)
    exit_with_error ("SDL_CreateRGBSurfaceFrom failed: %s", SDL_GetError ());
}

static void free_ascii_surface (void)
{
  SDL_DestroySurface (ascii_surface);
  free (ascii_data);
}

static void blit_string (SDL_Surface *surface, unsigned x_offset, unsigned y_offset, char *str)
{
  SDL_Rect dst, src;

  dst.x = x_offset;
  dst.y = y_offset;
  dst.h = PERFMETER_HEIGHT + 2;
  dst.w = dst.h / 2;

  src.y = 0;
  src.w = 10;
  src.h = 20;

  for (unsigned i = 0; str[i] != 0; i++) {
    unsigned n = str[i] - ' ';
    src.x = n * 10;
    SDL_BlitSurfaceScaled (ascii_surface, &src, surface, &dst, SDL_SCALEMODE_NEAREST);
    dst.x += dst.w;
  }
}

static void build_texture (ezv_ctx_t ctx)
{
  mon_render_ctx_t *renctx = ezv_mon_renderer (ctx);
  mon_obj_t *mon           = ezv_mon_mon (ctx);
  uint32_t *img_data = calloc (ctx->winw * ctx->winh, sizeof (uint32_t));

  SDL_Surface *s = SDL_CreateSurfaceFrom (ctx->winw, ctx->winh, SDL_PIXELFORMAT_RGBA32, img_data, ctx->winw * sizeof (uint32_t));
  if (s == NULL)
    exit_with_error ("SDL_CreateRGBSurfaceFrom failed: %s", SDL_GetError ());

  load_ascii_surface ();

  for (int c = 0; c < mon->cpu + mon->gpu; c++) {
    unsigned x_offset = MARGIN + LEGEND;
    unsigned y_offset = MARGIN + c * (PERFMETER_HEIGHT + INTERMARGIN);
    char msg[32];

    snprintf (msg, 32, "%cPU%3d ", c < mon->cpu ? 'C' : 'G', c < mon->cpu ? c : c - mon->cpu);
    blit_string (s, MARGIN, y_offset, msg);

    for (int i = 0; i < PERFMETER_HEIGHT; i++)
      for (int j = 0; j < PERFMETER_WIDTH; j++)
        img_data[(y_offset + i) * ctx->winw + x_offset + j] = ctx->cpu_colors[c];

    for (int i = 1; i < PERFMETER_HEIGHT - 1; i++)
      for (int j = 1; j < PERFMETER_WIDTH - 1; j++)
        img_data[(y_offset + i) * ctx->winw + x_offset + j] = ezv_rgb (0, 0, 0);
  }

  free_ascii_surface ();

  glGenTextures (1, &renctx->dataTexture);
  glActiveTexture (GL_TEXTURE0 + EZV_DATA_TEXTURE_NUM);
  glBindTexture (GL_TEXTURE_2D, renctx->dataTexture);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA32F, ctx->winw, ctx->winh, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, img_data);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  SDL_DestroySurface (s);
  free (img_data);

  // bind uniform buffer object to data texture
  glProgramUniform1i (renctx->data_shader, renctx->data_imgtex_loc,
                      EZV_DATA_TEXTURE_NUM);
}

void mon_renderer_set_mon (ezv_ctx_t ctx)
{
  mon_render_ctx_t *renctx = ezv_mon_renderer (ctx);
  mon_obj_t *mon           = ezv_mon_mon (ctx);

  ezv_switch_to_context (ctx);

  // Initialize 'Matrices'
  mon_renderer_mvp_init (ctx);

  float vertices[] = {
      // 2D positions     // tex coord
      0.0f, 0.0f, 0.0f, 0.0f, // top left
      0.0f, (float)(ctx->winh), 0.0f, 1.0f, // bottom left
      (float)(ctx->winw), 0.0f, 1.0f, 0.0f, // top right
      (float)(ctx->winw), (float)(ctx->winh), 1.0f, 1.0f, // bottom right
  };

  // Warning: use clockwise orientation
  unsigned int indices[] = {
      0, 3, 1, // first triangle
      0, 2, 3  // second triangle
  };

  // configure vertex attributes and misc buffers
  glGenVertexArrays (1, &renctx->VAO);
  glGenBuffers (1, &renctx->VBO);
  glGenBuffers (1, &renctx->VBO_IND);

  // bind the Vertex Array Object first, then bind and set vertex buffer(s)
  glBindVertexArray (renctx->VAO);

  glBindBuffer (GL_ARRAY_BUFFER, renctx->VBO);
  glBufferData (GL_ARRAY_BUFFER, sizeof (vertices), vertices, GL_STATIC_DRAW);

  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, renctx->VBO_IND);
  glBufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices), indices,
                GL_STATIC_DRAW);

  // configure vertex attributes(s).
  glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float),
                         (void *)0);
  glEnableVertexAttribArray (0);

  // configure texture coordinates
  glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float),
                         (void *)(2 * sizeof (float)));
  glEnableVertexAttribArray (1);

  // Perfmeters
  float cpuv[] = {
      // 2D positions
      0.0f,   0.0f,   // top left
      0.0f,   (float)PERFMETER_HEIGHT, // bottom left
      (float)PERFMETER_WIDTH, 0.0f,   // top right
      (float)PERFMETER_WIDTH, (float)PERFMETER_HEIGHT, // bottom right
  };

  glGenVertexArrays (1, &renctx->VAO_CPU);
  glGenBuffers (1, &renctx->VBO_CPU);

  // bind the Vertex Array Object first, then bind and set vertex buffer(s)
  glBindVertexArray (renctx->VAO_CPU);

  glBindBuffer (GL_ARRAY_BUFFER, renctx->VBO_CPU);
  glBufferData (GL_ARRAY_BUFFER, sizeof (cpuv), cpuv, GL_STATIC_DRAW);

  // We keep the same triangle indice buffer
  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, renctx->VBO_IND);

  // configure vertex attributes(s).
  glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof (float),
                         (void *)0);
  glEnableVertexAttribArray (0);

  // Texture Buffer Object containing RGBA colors
  glGenBuffers (1, &renctx->TBO_COL);
  glBindBuffer (GL_TEXTURE_BUFFER, renctx->TBO_COL);
  glBufferData (GL_TEXTURE_BUFFER, (mon->cpu + mon->gpu) * sizeof (int), NULL,
                GL_STATIC_DRAW); // Sent only once

  glActiveTexture (GL_TEXTURE0 + EZV_CPU_TEXTURE_NUM);
  glGenTextures (1, &renctx->tex_color);
  glBindTexture (GL_TEXTURE_BUFFER, renctx->tex_color);
  glTexBuffer (GL_TEXTURE_BUFFER, GL_R32I, renctx->TBO_COL);

  // bind uniform buffer object to cpu texture
  glProgramUniform1i (renctx->cpu_shader, renctx->cpu_colors_loc,
                      EZV_CPU_TEXTURE_NUM);
}

void mon_set_data_colors (ezv_ctx_t ctx, void *values)
{
  mon_render_ctx_t *renctx = ezv_mon_renderer (ctx);
  mon_obj_t *mon           = ezv_mon_mon (ctx);

  ezv_switch_to_context (ctx);

  glProgramUniform1fv (renctx->cpu_shader, renctx->cpu_ratios_loc,
                       mon->cpu + mon->gpu, values);
}

static void transfer_rgba_colors (ezv_ctx_t ctx)
{
  if (ezv_palette_is_defined (&ctx->cpu_palette)) {
    mon_render_ctx_t *renctx = ezv_mon_renderer (ctx);
    mon_obj_t *mon = ezv_mon_mon (ctx);

    glBindBuffer (GL_TEXTURE_BUFFER, renctx->TBO_COL);
    glBufferSubData (GL_TEXTURE_BUFFER, 0, (mon->cpu + mon->gpu) * sizeof (int),
                     ctx->cpu_colors);
  } else
    exit_with_error ("CPU palette unconfigured\n");

  build_texture (ctx);
}

void mon_render (ezv_ctx_t ctx)
{
  mon_render_ctx_t *renctx = ezv_mon_renderer (ctx);
  mon_obj_t *mon           = ezv_mon_mon (ctx);

  ezv_switch_to_context (ctx);

  glClearColor (0.0f, 0.2f, 0.2f, 1.0f);

  glClear (GL_COLOR_BUFFER_BIT |
           GL_DEPTH_BUFFER_BIT); // also clear the depth buffer now!

  // transfer RGBA cpu colors only once
  static int done = 0;
  if (!done) {
    transfer_rgba_colors (ctx);
    done = 1;
  }

  // Background image
  glBindVertexArray (renctx->VAO);
  glUseProgram (renctx->data_shader);

  glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

  // Perf meters
  glBindVertexArray (renctx->VAO_CPU);
  glUseProgram (renctx->cpu_shader);

  glDrawElementsInstanced (GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0, mon->cpu + mon->gpu);

  SDL_GL_SwapWindow (ctx->win);
}
