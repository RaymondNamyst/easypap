#include <stdlib.h>

#include "ezv_img2d_object.h"
#include "img2d_renderer.h"

unsigned img2d_get_color_data_size (ezv_ctx_t ctx);
unsigned img2d_get_linepitch (ezv_ctx_t ctx);

static ezv_class_t the_class = {
  img2d_render, // render
  img2d_reset_view, // reset view
  img2d_get_color_data_size, // get_color_data_size
  img2d_renderer_use_cpu_palette, // activate_rgba_palette
  img2d_renderer_use_data_palette, // acticate_data_palette
  img2d_get_shareable_buffer_ids, // shareable buffers
  img2d_set_data_brightness, // data brightness
  NULL, // 1D picking
  img2d_renderer_do_picking, // 2D picking
  img2d_renderer_zoom, // zoom
  img2d_renderer_motion, // motion
  NULL,  // move z plane
  img2d_set_data_colors, // set data colors
  img2d_get_linepitch, // line pitch
  NULL // screenshot
};

void ezv_img2d_object_init (ezv_ctx_t ctx)
{
  ctx->class = &the_class;
  ctx->object = calloc (1, sizeof (ezv_img2d_object_t));

  // Initialize main renderer
  img2d_renderer_init (ctx);
}

void ezv_img2d_set_img (ezv_ctx_t ctx, img2d_obj_t *img)
{
  ezv_img2d_object_t *obj = ctx->object;

  obj->img = img;

  // tell renderer that img is known
  img2d_renderer_set_img (ctx);
}

void ezv_img2d_set_renderer (ezv_ctx_t ctx, struct img2d_render_ctx_s *ren)
{
  ezv_img2d_object_t *obj = ctx->object;

  obj->render_ctx = ren;
}

// Private virtual methods

unsigned img2d_get_color_data_size (ezv_ctx_t ctx)
{
  img2d_obj_t *img = ezv_img2d_img (ctx);

  return img->width * img->height;
}

unsigned img2d_get_linepitch (ezv_ctx_t ctx)
{
  img2d_obj_t *img = ezv_img2d_img (ctx);

  return img->width;
}
