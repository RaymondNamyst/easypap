
#include "ezv_virtual.h"
#include "error.h"
#include "ezv_ctx.h"
#include "ezv_event.h"

void ezv_render (ezv_ctx_t ctx[], unsigned nb_ctx)
{
  for (int c = 0; c < nb_ctx; c++)
    ctx[c]->class->render (ctx[c]);
}

void ezv_reset_view (ezv_ctx_t ctx[], unsigned nb_ctx)
{
  for (int c = 0; c < nb_ctx; c++)
    ctx[c]->class->reset_view (ctx + c, 1);
}

unsigned ezv_get_color_data_size (ezv_ctx_t ctx)
{
  return ctx->class->get_color_data_size (ctx);
}

void ezv_activate_rgba_palette (ezv_ctx_t ctx)
{
  ctx->class->activate_rgba_palette (ctx);
}

void ezv_activate_data_palette (ezv_ctx_t ctx)
{
  ctx->class->activate_data_palette (ctx);
}

void ezv_switch_color_buffers (ezv_ctx_t ctx)
{
  if (ctx->class->switch_color_buffers != NULL)
    ctx->class->switch_color_buffers (ctx);
}

void ezv_get_shareable_buffer_ids (ezv_ctx_t ctx, int buffer_ids[])
{
  ctx->class->get_shareable_buffer_ids (ctx, buffer_ids);
}

void ezv_set_data_brightness (ezv_ctx_t ctx, float brightness)
{
  ctx->class->set_data_brightness (ctx, brightness);
}

int ezv_do_1D_picking (ezv_ctx_t ctx, int mousex, int mousey)
{
  if (ctx->class->do_1D_picking != NULL)
    return ctx->class->do_1D_picking (ctx, mousex, mousey);
  else
    exit_with_error ("1D picking not supported on %s ctx", ezv_ctx_type (ctx));
}

void ezv_do_2D_picking (ezv_ctx_t ctx, int mousex, int mousey, int *x, int *y)
{
  if (ctx->class->do_2D_picking != NULL)
    ctx->class->do_2D_picking (ctx, mousex, mousey, x, y);
  else
    exit_with_error ("2D picking not supported on %s ctx", ezv_ctx_type (ctx));
}

int ezv_zoom (ezv_ctx_t ctx[], unsigned nb_ctx, unsigned shift_mod, unsigned in)
{
  return ctx[0]->class->zoom (ctx, nb_ctx, shift_mod, in);
}

int ezv_motion (ezv_ctx_t ctx[], unsigned nb_ctx, int dx, int dy,
                unsigned wheel)
{
  return ctx[0]->class->motion (ctx, nb_ctx, dx, dy, wheel);
}

void ezv_move_zplane (ezv_ctx_t ctx[], unsigned nb_ctx, float dz)
{
  if (ctx[0]->class->move_zplane != NULL)
    ctx[0]->class->move_zplane (ctx, nb_ctx, dz);
}

void ezv_set_data_colors (ezv_ctx_t ctx, void *values)
{
  ctx->class->set_data_colors (ctx, values);
}

unsigned ezv_get_linepitch (ezv_ctx_t ctx)
{
  if (ctx->class->get_linepitch != NULL)
    return ctx->class->get_linepitch (ctx);
  else
    exit_with_error ("get_linepitch not supported on %s ctx", ezv_ctx_type (ctx));
}
