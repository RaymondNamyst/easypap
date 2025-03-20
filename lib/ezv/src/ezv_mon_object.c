#include <stdlib.h>

#include "ezv_mon_object.h"
#include "mon_renderer.h"

unsigned mon_get_color_data_size (ezv_ctx_t ctx);

static ezv_class_t the_class = {
  mon_render, // render
  NULL, // reset view
  mon_get_color_data_size, // get_color_data_size
  NULL, // activate_rgba_palette
  NULL, // acticate_data_palette
  NULL, // shareable buffers
  NULL, // data brightness
  NULL, // 1D picking
  NULL, // 2D picking
  NULL, // zoom
  NULL, // motion
  NULL,  // move z plane
  mon_set_data_colors, // set data colors
  NULL, // line pitch
  NULL // screenshot
};

void ezv_mon_object_init (ezv_ctx_t ctx)
{
  ctx->class = &the_class;
  ctx->object = calloc (1, sizeof (ezv_mon_object_t));

  // Initialize main renderer
  mon_renderer_init (ctx);
}

void ezv_mon_set_moninfo (ezv_ctx_t ctx, mon_obj_t *mon)
{
  ezv_mon_object_t *obj = ctx->object;

  obj->mon = mon;

  // tell renderer that mon info is known
  mon_renderer_set_mon (ctx);
}

void ezv_mon_set_renderer (ezv_ctx_t ctx, struct mon_render_ctx_s *ren)
{
  ezv_mon_object_t *obj = ctx->object;

  obj->render_ctx = ren;
}

// Private virtual methods

unsigned mon_get_color_data_size (ezv_ctx_t ctx)
{
  mon_obj_t *mon = ezv_mon_mon (ctx);

  return mon->cpu + mon->gpu;
}
