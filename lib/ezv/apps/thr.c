
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "error.h"
#include "ezv.h"
#include "ezv_event.h"

// settings
const unsigned int SCR_WIDTH  = 1024;
const unsigned int SCR_HEIGHT = 768;

#define MAX_CTX 2

static mesh3d_obj_t mesh;
static ezv_ctx_t ctx[MAX_CTX] = {NULL, NULL};
static unsigned nb_ctx        = 1;
static int hud                = -1;

static void do_pick (void)
{
  int p = ezv_perform_1D_picking (ctx, nb_ctx);

  if (p == -1)
    ezv_hud_off (ctx[0], hud);
  else {
    ezv_hud_on (ctx[0], hud);
    ezv_hud_set (ctx[0], hud, "Cell: %d", p);
  }
}

enum
{
  EZV_THR_EVENT_DATA_COLORS,
  EZV_THR_EVENT_CPU_COLORS
};

static uint32_t base_event = 0;

static void thr_push_data_colors (ezv_ctx_t ctx, void *values)
{
  SDL_Event event;

  event.type       = SDL_EVENT_USER;
  event.user.code  = base_event + EZV_THR_EVENT_DATA_COLORS;
  event.user.data1 = (void *)ctx;
  event.user.data2 = (void *)values;

  SDL_PushEvent (&event);
}

static void process_events (void)
{
  SDL_Event event;
  int r;

  r = ezv_get_event (&event, 1);

  if (r > 0) {
    int pick = 0;
    if (event.type == SDL_EVENT_USER) {
      if (event.user.code == base_event + EZV_THR_EVENT_DATA_COLORS) {
        ezv_set_data_colors ((ezv_ctx_t)event.user.data1,
                             (float *)event.user.data2);
        pick = 1;
      }
    } else
      ezv_process_event (ctx, nb_ctx, &event, NULL, &pick);
    if (pick)
      do_pick ();
  }
}

static void thr_init (void)
{
  base_event = SDL_RegisterEvents (1);
}

static float *values = NULL;

void compute (void)
{
  static float offset = 0.0;

  // Color cells according to their position within the bounding box
  const int COORD = 2;
  for (int c = 0; c < mesh.nb_cells; c++) {
    bbox_t box;
    mesh3d_obj_get_bbox_of_cell (&mesh, c, &box);
    float z   = (box.min[COORD] + box.max[COORD]) / 2.0f;
    values[c] = (z - mesh.bbox.min[COORD]) /
                (mesh.bbox.max[COORD] - mesh.bbox.min[COORD]);
    values[c] += offset;
    if (values[c] > 1.0)
      values[c] = values[c] - 1.0;
  }

  offset += .01;
  if (offset > 1.0)
    offset = 0.0;

  usleep (200000);
}

static void *compute_loop (void *arg)
{
  pthread_detach (pthread_self ());

  while (1) {
    compute ();
    thr_push_data_colors (ctx[0], values);
  }

  return NULL;
}

int main (int argc, char *argv[])
{
  ezv_init ();

  thr_init ();

  mesh3d_obj_init (&mesh);

  if (argc > 1)
    mesh3d_obj_load (argv[1], &mesh);
  else
    mesh3d_obj_build_torus_surface (&mesh, 1);

  // Create SDL windows and initialize OpenGL context
  ctx[0] = ezv_ctx_create (EZV_CTX_TYPE_MESH3D, "Mesh", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_UNDEFINED, SCR_WIDTH, SCR_HEIGHT,
                           EZV_ENABLE_PICKING | EZV_ENABLE_HUD |
                               EZV_ENABLE_CLIPPING);
  hud    = ezv_hud_alloc (ctx[0]);
  ezv_hud_on (ctx[0], hud);

  // Attach mesh
  ezv_mesh3d_set_mesh (ctx[0], &mesh);

  // Heat Palette (as simple as defining these five key colors)
  float colors[] = {0.0f, 0.0f, 1.0f, 1.f,  // blue
                    0.0f, 1.0f, 1.0f, 1.f,  // cyan
                    0.0f, 1.0f, 0.0f, 1.f,  // green
                    1.0f, 1.0f, 0.0f, 1.f,  // yellow
                    1.0f, 0.0f, 0.0f, 1.f,  // red
                    0.5f, 0.0f, 1.0f, 1.f,  // purple
                    0.0f, 0.0f, 1.0f, 1.f}; // blue

  ezv_use_data_colors (ctx[0], colors, 7);

  values = malloc (mesh.nb_cells * sizeof (float));

  pthread_t pid;
  pthread_create (&pid, NULL, compute_loop, NULL);

  // render loop
  while (1) {
    process_events ();
    ezv_render (ctx, nb_ctx);
  }

  free (values);

  return 0;
}
