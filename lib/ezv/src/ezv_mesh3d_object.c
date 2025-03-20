#include <stdlib.h>

#include "ezv_mesh3d_object.h"
#include "mesh3d_renderer.h"

unsigned mesh3d_get_color_data_size (ezv_ctx_t ctx);


static ezv_class_t the_class = {
  mesh3d_render, // render
  mesh3d_reset_view, // reset view
  mesh3d_get_color_data_size, // get_color_data_size
  mesh3d_renderer_use_cpu_palette, // activate_rgba_palette
  mesh3d_renderer_use_data_palette, // activate_data_palette
  mesh3d_get_shareable_buffer_ids, // shareable buffers
  mesh3d_set_data_brightness, // data brightness
  mesh3d_renderer_do_picking, // 1D picking
  NULL, // 2D picking
  mesh3d_renderer_zoom, // zoom
  mesh3d_renderer_motion, // motion
  mesh3d_renderer_zplane_update, // move z plane
  mesh3d_set_data_colors, // set data colors
  NULL, // line pitch
  mesh3d_screenshot // screenshot
};

void ezv_mesh3d_object_init (ezv_ctx_t ctx)
{
  ctx->class = &the_class;
  ctx->object = calloc (1, sizeof (ezv_mesh3d_object_t));

  // Initialize main renderer
  mesh3d_renderer_init (ctx);
}

void ezv_mesh3d_set_mesh (ezv_ctx_t ctx, mesh3d_obj_t *mesh)
{
  ezv_mesh3d_object_t *obj = ctx->object;

  obj->mesh = mesh;

  // tell renderer that mesh is known
  mesh3d_renderer_set_mesh (ctx);
}

void ezv_mesh3d_set_renderer (ezv_ctx_t ctx, struct render_ctx_s *ren)
{
  ezv_mesh3d_object_t *obj = ctx->object;

  obj->render_ctx = ren;
}

// Private virtual methods

unsigned mesh3d_get_color_data_size (ezv_ctx_t ctx)
{
  mesh3d_obj_t *mesh = ezv_mesh3d_mesh (ctx);

  return mesh->nb_cells;
}
