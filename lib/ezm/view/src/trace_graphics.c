#include "trace_graphics.h"
#include "error.h"
#include "ezv.h"
#include "ezv_sdl_gl.h"
#include "trace_colors.h"

#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <fcntl.h>
#include <fut.h>
#include <fxt-tools.h>
#include <fxt.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

enum
{
  EASYVIEW_MODE_UNDEFINED,
  EASYVIEW_MODE_2D_IMAGES,
  EASYVIEW_MODE_3D_MESHES
} easyview_mode = EASYVIEW_MODE_UNDEFINED;

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define MAX_FILENAME 1024

// How much percentage of duration should we shift ?
#define SHIFT_FACTOR 0.02
#define MIN_DURATION 100.0

#define WINDOW_MIN_WIDTH 1024
// no WINDOW_MIN_HEIGHT: needs to be automatically computed

#define MIN_TASK_HEIGHT 6
#define MAX_TASK_HEIGHT 44

#define MAX_CACHE_WIDTH (100 + 2)

#define MAGNIFICATION 2
#define Y_MARGIN 2
#define cpu_row_height(taskh) ((taskh) + 2 * Y_MARGIN + 1)
#define task_height(space) ((space) - 2 * Y_MARGIN - 1)

#define GANTT_WIDTH                                                            \
  (WINDOW_WIDTH - (LEFT_MARGIN + RIGHT_MARGIN + CACHE_STATS_WIDTH))
#define LEFT_MARGIN 80
#define RIGHT_MARGIN 24
#define TOP_MARGIN 48
#define FONT_HEIGHT 20
#define BOTTOM_MARGIN (FONT_HEIGHT + 4)
#define INTERTRACE_MARGIN (2 * FONT_HEIGHT + 2)

#define SQUARE_SIZE 16
#define TILE_ALPHA 0x80
#define BUTTON_ALPHA 60

#define BLACK_COL ezv_rgb (0, 0, 0)
#define DARK_COL ezv_rgb (60, 60, 60)
#define WHITE_COL ezv_rgb (255, 255, 255)

static SDL_Color silver_color  = {192, 192, 192, 255};
static SDL_Color backgrd_color = {0, 51, 51, 255}; //{50, 50, 65, 255};

static SDL_Texture *black_square     = NULL;
static SDL_Texture *dark_square      = NULL;
static SDL_Texture *white_square     = NULL;
static SDL_Texture *stat_frame_tex   = NULL;
static SDL_Texture *stat_caption_tex = NULL;
static SDL_Texture *stat_background  = NULL;

static void **thumb_data[MAX_TRACES] = {NULL, NULL};

static int TASK_HEIGHT   = MAX_TASK_HEIGHT;
static int WINDOW_HEIGHT = -1;
static int WINDOW_WIDTH  = -1;

static int GANTT_HEIGHT, CACHE_STATS_WIDTH;

static int BUBBLE_WIDTH, BUBBLE_HEIGHT, REDUCED_BUBBLE_WIDTH,
    REDUCED_BUBBLE_HEIGHT;

static Uint32 main_windowID = 0;

static SDL_Window *window             = NULL;
static SDL_Renderer *renderer         = NULL;
static TTF_Font *the_font             = NULL;
static SDL_Texture **perf_fill        = NULL;
static SDL_Texture *text_texture      = NULL;
static SDL_Texture *vertical_line     = NULL;
static SDL_Texture *horizontal_line   = NULL;
static SDL_Texture *horizontal_bis    = NULL;
static SDL_Texture *bulle_tex         = NULL;
static SDL_Texture *reduced_bulle_tex = NULL;
static SDL_Texture *us_tex            = NULL;
static SDL_Texture *sigma_tex         = NULL;
static SDL_Texture *tab_left          = NULL;
static SDL_Texture *tab_right         = NULL;
static SDL_Texture *tab_high          = NULL;
static SDL_Texture *tab_low           = NULL;
static SDL_Texture *align_tex         = NULL;
static SDL_Texture *quick_nav_tex     = NULL;
static SDL_Texture *track_tex         = NULL;
static SDL_Texture *footprint_tex     = NULL;
static SDL_Texture *digit_tex[10]     = {NULL};
static SDL_Texture *mouse_tex         = NULL;

static SDL_FRect align_rect, quick_nav_rect, track_rect, footprint_rect;

static unsigned digit_tex_width[10];
static unsigned digit_tex_height;

static int quick_nav_mode = 0;
static int horiz_mode     = 0;
static int tracking_mode  = 0;
static int footprint_mode = 0;
static int backlog_mode   = 0;

static long start_time = 0, end_time = 0, duration = 0;

static long selection_start_time = 0, selection_duration = 0;
static long mouse_orig_time    = 0;
static SDL_Point mouse         = {-1, -1};
static int mouse_in_gantt_zone = 0;
static int mouse_down          = 0;

static int max_cores = -1, max_iterations = -1;
static long max_time = -1;

unsigned char brightness = 150;

extern char *trace_dir[]; // Defined in main.c

static char easyview_img_dir[1024];
static char easyview_font_dir[1024];
static char easyview_ezv_dir[1024];

struct
{
  SDL_Rect gantt;
  SDL_Texture *label_tex;
  unsigned label_width, label_height;
  SDL_Texture **task_ids_tex;
  unsigned *task_ids_tex_width;
} trace_display_info[MAX_TRACES]; // no more than MAX_TRACES simultaneous traces

static SDL_Rect gantts_bounding_box;

struct
{
  int first_displayed_iter, last_displayed_iter;
} trace_ctrl[MAX_TRACES];

static ezv_ctx_t ctx[MAX_TRACES] = {NULL, NULL};

static SDL_Point mouse_pick = {-1, 0};
// 3D meshes
static mesh3d_obj_t mesh;
// 2D images
static img2d_obj_t img2d;

static void find_shared_directories (void)
{
  char *pi = stpcpy (easyview_img_dir, SDL_GetBasePath ());
  char *pf = stpcpy (easyview_font_dir, easyview_img_dir);
  char *pv = stpcpy (easyview_ezv_dir, easyview_img_dir);

#ifdef INSTALL_DIR // compiled with CMAKE
  strcpy (pi, "../share/ezm/img/");
  strcpy (pf, "../share/ezm/fonts/");
  strcpy (pv, "../");
#else
  strcpy (pi, "../share/ezm/img/");
  strcpy (pf, "../share/ezm/fonts/");
  strcpy (pv, "../../../ezv");
#endif
}

static SDL_Surface *load_img (const char *filename)
{
  char path[1024];
  SDL_Surface *s;
  char *p = stpcpy (path, easyview_img_dir);
  strcpy (p, filename);

  s = IMG_Load (path);
  if (s == NULL)
    exit_with_error ("IMG_Load (%s) failed: %s", filename, SDL_GetError ());

  return s;
}

static TTF_Font *load_font (const char *filename, int ptsize)
{
  char path[1024];
  TTF_Font *f;
  char *p = stpcpy (path, easyview_font_dir);
  strcpy (p, filename);

  f = TTF_OpenFont (path, ptsize);
  if (f == NULL)
    exit_with_error ("TTF_OpenFont (%s) failed: %s", filename, SDL_GetError ());

  return f;
}

static inline unsigned layout_get_min_width (void)
{
  return WINDOW_MIN_WIDTH;
}

static unsigned layout_get_height (unsigned task_height)
{
  unsigned need_left, need_right, gantt_h;

  if (nb_traces == 1) {
    need_right = TOP_MARGIN + BOTTOM_MARGIN;
    gantt_h    = trace[0].nb_cores * cpu_row_height (task_height);
    need_left  = TOP_MARGIN + gantt_h + BOTTOM_MARGIN;
  } else {
    need_right = TOP_MARGIN + 2 * INTERTRACE_MARGIN + BOTTOM_MARGIN;
    gantt_h =
        (trace[0].nb_cores + trace[1].nb_cores) * cpu_row_height (task_height) +
        INTERTRACE_MARGIN;
    need_left = TOP_MARGIN + gantt_h + BOTTOM_MARGIN;
  }
  return max (need_left, need_right);
}

static unsigned layout_get_min_height (void)
{
  return layout_get_height (MIN_TASK_HEIGHT);
}

static unsigned layout_get_max_height (void)
{
  return layout_get_height (MAX_TASK_HEIGHT);
}

static void layout_place_buttons (void)
{
  quick_nav_rect.x = trace_display_info[0].gantt.x +
                     trace_display_info[0].gantt.w - quick_nav_rect.w;
  quick_nav_rect.y = 2;

  align_rect.x = quick_nav_rect.x - Y_MARGIN - align_rect.w;
  align_rect.y = 2;

  track_rect.x = align_rect.x - Y_MARGIN - track_rect.w;
  track_rect.y = 2;

  footprint_rect.x = track_rect.x - Y_MARGIN - footprint_rect.w;
  footprint_rect.y = 2;
}

static void layout_recompute (int at_init)
{
  unsigned need_left;

  if (nb_traces == 1) {
    // See how much space we have for GANTT chart
    unsigned space = WINDOW_HEIGHT - TOP_MARGIN - BOTTOM_MARGIN;
    space /= trace[0].nb_cores;
    TASK_HEIGHT = task_height (space);
    if (TASK_HEIGHT < MIN_TASK_HEIGHT)
      exit_with_error ("Window height (%d) is not big enough to display so "
                       "many CPUS (%d)\n",
                       WINDOW_HEIGHT, trace[0].nb_cores);

    if (TASK_HEIGHT > MAX_TASK_HEIGHT)
      TASK_HEIGHT = MAX_TASK_HEIGHT;

    if (at_init)
      WINDOW_HEIGHT = layout_get_height (TASK_HEIGHT);

    GANTT_HEIGHT = trace[0].nb_cores * cpu_row_height (TASK_HEIGHT);

    CACHE_STATS_WIDTH =
        trace[0].has_cache_data ? MAX_CACHE_WIDTH + RIGHT_MARGIN : 0;
    trace_display_info[0].gantt.x = LEFT_MARGIN;
    trace_display_info[0].gantt.y = TOP_MARGIN;
    trace_display_info[0].gantt.w = GANTT_WIDTH;
    trace_display_info[0].gantt.h = GANTT_HEIGHT;
  } else {
    unsigned padding = 0;

    // See how much space we have for GANTT chart
    unsigned space =
        WINDOW_HEIGHT - TOP_MARGIN - BOTTOM_MARGIN - INTERTRACE_MARGIN;
    space /= (trace[0].nb_cores + trace[1].nb_cores);
    TASK_HEIGHT = task_height (space);
    if (TASK_HEIGHT < MIN_TASK_HEIGHT)
      exit_with_error ("Window height (%d) is not big enough to display so "
                       "many CPUS (%d)\n",
                       WINDOW_HEIGHT, trace[0].nb_cores);

    if (TASK_HEIGHT > MAX_TASK_HEIGHT)
      TASK_HEIGHT = MAX_TASK_HEIGHT;

    if (at_init)
      WINDOW_HEIGHT = layout_get_height (TASK_HEIGHT);

    CACHE_STATS_WIDTH = (trace[0].has_cache_data || trace[1].has_cache_data)
                            ? MAX_CACHE_WIDTH + RIGHT_MARGIN
                            : 0;

    // First try with max task height
    GANTT_HEIGHT =
        (trace[0].nb_cores + trace[1].nb_cores) * cpu_row_height (TASK_HEIGHT) +
        INTERTRACE_MARGIN;
    need_left = TOP_MARGIN + GANTT_HEIGHT + BOTTOM_MARGIN;

    if (WINDOW_HEIGHT > need_left)
      padding = WINDOW_HEIGHT - need_left;

    trace_display_info[0].gantt.x = LEFT_MARGIN;
    trace_display_info[0].gantt.y = TOP_MARGIN + padding / 2;
    trace_display_info[0].gantt.w = GANTT_WIDTH;
    trace_display_info[0].gantt.h =
        trace[0].nb_cores * cpu_row_height (TASK_HEIGHT);

    trace_display_info[1].gantt.x = LEFT_MARGIN;
    trace_display_info[1].gantt.y = TOP_MARGIN + INTERTRACE_MARGIN +
                                    trace_display_info[0].gantt.h + padding / 2;
    trace_display_info[1].gantt.w = GANTT_WIDTH;
    trace_display_info[1].gantt.h =
        trace[1].nb_cores * cpu_row_height (TASK_HEIGHT);
  }

  gantts_bounding_box.x = trace_display_info[0].gantt.x;
  gantts_bounding_box.y = trace_display_info[0].gantt.y;
  gantts_bounding_box.w = trace_display_info[0].gantt.w;
  gantts_bounding_box.h = (trace_display_info[nb_traces - 1].gantt.y +
                           trace_display_info[nb_traces - 1].gantt.h - 1) -
                          gantts_bounding_box.y;

  // printf ("Window initial size: %dx%d\n", WINDOW_WIDTH, WINDOW_HEIGHT);
}

static inline int time_to_pixel (long time)
{
  return LEFT_MARGIN + time * GANTT_WIDTH / duration -
         start_time * GANTT_WIDTH / duration;
}

static inline long pixel_to_time (int x)
{
  return start_time + (x - LEFT_MARGIN) * duration / GANTT_WIDTH;
}

static inline int point_in_xrange (const SDL_Rect *r, int x)
{
  return x >= r->x && x < (r->x + r->w);
}

static inline int point_in_yrange (const SDL_Rect *r, int y)
{
  return y >= r->y && y < (r->y + r->h);
}

static inline int point_in_rect (const SDL_Point *p, const SDL_Rect *r)
{
  return point_in_xrange (r, p->x) && point_in_yrange (r, p->y);
}

static inline int point_inside_mosaic (const SDL_Point *p, unsigned trace_num)
{
  return ezv_ctx_is_in_focus (ctx[trace_num]);
}

static inline int point_inside_mosaics (const SDL_Point *p)
{
  return point_inside_mosaic (p, 0) ||
         ((nb_traces == 1) ? 0 : (point_inside_mosaic (p, 1)));
}

static inline int point_inside_gantt (const SDL_Point *p, unsigned trace_num)
{
  return point_in_rect (p, &trace_display_info[trace_num].gantt);
}

static inline int point_inside_gantts (const SDL_Point *p)
{
  return point_in_rect (p, &gantts_bounding_box);
}

static inline int rects_do_intersect (const SDL_Rect *r1, const SDL_Rect *r2)
{
  return SDL_HasRectIntersection (r1, r2);
}

static inline void get_raw_rect (trace_task_t *t, SDL_Rect *dst)
{
  dst->x = t->x;
  dst->w = t->w;

  dst->y = t->y;
  if (easyview_mode != EASYVIEW_MODE_3D_MESHES)
    dst->h = t->h;
  else
    dst->h = 1;
}

static SDL_FRect rect2frect (SDL_Rect *r)
{
  return (SDL_FRect){r->x, r->y, r->w, r->h};
}

static int get_y_mouse_sibbling (void)
{
  for (int t = 0; t < nb_traces; t++) {
    if (point_in_rect (&mouse, &trace_display_info[t].gantt)) {
      int dy = mouse.y - trace_display_info[t].gantt.y;
      if (dy < trace_display_info[1 - t].gantt.h)
        return trace_display_info[1 - t].gantt.y + dy;
      else
        return mouse.y;
    }
  }
  return mouse.y;
}

static inline int is_gpu (trace_t *const tr, int cpu_num)
{
  return cpu_num >= (tr->nb_cores - tr->nb_gpu);
}

static inline int is_lane (trace_t *const tr, int cpu_num)
{
  int tot = tr->nb_cores - tr->nb_gpu;
  if (cpu_num >= tot)
    return (cpu_num - tot) & 1; // #GPU is odd

  return 0;
}

// Texture creation functions

static float *mesh_load_raw_data (const char *filename)
{
  int fd = open (filename, O_RDONLY);
  if (fd == -1)
    return NULL;

  off_t len = lseek (fd, 0L, SEEK_END);
  if (len % sizeof (float) != 0)
    exit_with_error ("%s size should be a multiple of sizeof(float)", filename);

  lseek (fd, 0L, SEEK_SET);

  float *data = malloc (len);
  int n       = read (fd, data, len);
  if (n != len)
    exit_with_error ("Read from %s returned less data than expected", filename);

  close (fd);
  return data;
}

static unsigned preload_thumbnails (unsigned nb_iter)
{
  unsigned success = 0, expected = 0;

  unsigned nb_dirs  = 0;
  unsigned bound[2] = {nb_iter, nb_iter};
  char *dir[2]      = {trace_dir[0], trace_dir[1]};

  if (trace_dir[1] ||
      trace[0].first_iteration != trace[nb_traces - 1].first_iteration) {
    // Ok, we have to use two separate arrays to store textures,
    // either because thumbnails are located in two separate folders
    // or because we compare two traces starting from a different iteration
    // number. (Note that in this latter case -- and if iteration ranges
    // overlap -- we could probably try to load once and shift the indexes in
    // the second array... I don't think it is worth it.)

    nb_dirs = 2;
    if (!dir[1])
      dir[1] = dir[0];
    bound[0]      = trace[0].nb_iterations;
    bound[1]      = trace[1].nb_iterations;
    expected      = bound[0] + bound[1];
    thumb_data[0] = malloc (bound[0] * sizeof (void *));
    thumb_data[1] = malloc (bound[1] * sizeof (void *));
  } else {
    // We use a unique array to store thumbnails, either because we're
    // displaying a single trace or because we compare two traces with one
    // iteration range being a subset of the other (in this case, nb_iter is
    // the maximum number of iterations).
    nb_dirs       = 1;
    expected      = bound[0];
    thumb_data[0] = malloc (nb_iter * sizeof (void *));
    thumb_data[1] = thumb_data[0];
  }

  for (int d = 0; d < nb_dirs; d++)
    for (int iter = 0; iter < bound[d]; iter++) {
      void *thumb = NULL;
      char filename[MAX_FILENAME];
      img2d_obj_t thumb2d;
      img2d_obj_init (&thumb2d, trace[0].dimensions, trace[0].dimensions);

      if (easyview_mode == EASYVIEW_MODE_2D_IMAGES) {
        sprintf (filename, "%s/thumb_%04d.png", dir[d],
                 trace[d].first_iteration + iter);
        if (access (filename, R_OK) != -1) {
          thumb = malloc (img2d_obj_size (&thumb2d));
          img2d_obj_load_resized (&thumb2d, filename, thumb);
        }
      } else {
        sprintf (filename, "%s/thumb_%04d.raw", dir[d],
                 trace[d].first_iteration + iter);
        thumb = mesh_load_raw_data (filename);
      }

      if (thumb != NULL) {
        success++;
        thumb_data[d][iter] = thumb;
      } else
        thumb_data[d][iter] = NULL;
    }

  printf ("%d/%u thumbnails successfully preloaded\n", success, expected);

  return success;
}

static void create_task_textures (void)
{
  Uint32 *restrict img = malloc (GANTT_WIDTH * TASK_HEIGHT * sizeof (Uint32));

  perf_fill = malloc ((TRACE_MAX_COLORS + 1) * sizeof (SDL_Texture *));

  SDL_Surface *s =
      SDL_CreateSurfaceFrom (GANTT_WIDTH, TASK_HEIGHT, SDL_PIXELFORMAT_RGBA32,
                             img, GANTT_WIDTH * sizeof (Uint32));
  if (s == NULL)
    exit_with_error ("SDL_CreateRGBSurfaceFrom () failed");

  unsigned largeur_couleur_origine = GANTT_WIDTH / 4;
  unsigned largeur_degrade         = GANTT_WIDTH - largeur_couleur_origine;
  float attenuation_depart         = 1.0;
  float attenuation_finale         = 0.3;

  for (int c = 0; c < TRACE_MAX_COLORS + 1; c++) {
    bzero (img, GANTT_WIDTH * TASK_HEIGHT * sizeof (Uint32));

    if (c == TRACE_MAX_COLORS) // special treatment for white color
      attenuation_finale = 0.5;

    for (int j = 0; j < GANTT_WIDTH; j++) {
      uint32_t couleur = trace_cpu_color (c);
      uint8_t r        = ezv_c2r (couleur);
      uint8_t g        = ezv_c2g (couleur);
      uint8_t b        = ezv_c2b (couleur);

      if (j >= largeur_couleur_origine) {
        float coef =
            attenuation_depart -
            ((((float)(j - largeur_couleur_origine)) / largeur_degrade)) *
                (attenuation_depart - attenuation_finale);
        r = r * coef;
        g = g * coef;
        b = b * coef;
      }

      for (int i = 0; i < TASK_HEIGHT; i++)
        img[i * GANTT_WIDTH + j] = ezv_rgb (r, g, b);
    }

    perf_fill[c] = SDL_CreateTextureFromSurface (renderer, s);
  }

  SDL_DestroySurface (s);
  free (img);

  // Cache stats background
  const unsigned width  = MAX_CACHE_WIDTH;
  const unsigned height = 18;

  img = malloc (width * height * sizeof (Uint32));
  s   = SDL_CreateSurfaceFrom (width, height, SDL_PIXELFORMAT_RGBA32, img,
                               width * sizeof (Uint32));
  if (s == NULL)
    exit_with_error ("SDL_CreateRGBSurfaceFrom () failed");

  attenuation_depart = 0.;
  attenuation_finale = 1.0;

  for (int j = 0; j < width; j++) {
    uint8_t r = 130;
    uint8_t g = 130;
    uint8_t b = 165;

    float coef =
        attenuation_depart +
        (((float)j) / width) * (attenuation_finale - attenuation_depart);

    r = r * coef + backgrd_color.r * (1.0 - coef);
    g = g * coef + backgrd_color.g * (1.0 - coef);
    b = b * coef + backgrd_color.b * (1.0 - coef);

    for (int i = 0; i < height; i++)
      img[i * width + j] = ezv_rgb (r, g, b);
  }

  stat_background = SDL_CreateTextureFromSurface (renderer, s);

  SDL_DestroySurface (s);
  free (img);

  // Cache stats frame
  img = malloc (MAX_CACHE_WIDTH * (MIN_TASK_HEIGHT + 2) * sizeof (Uint32));
  s   = SDL_CreateSurfaceFrom (MAX_CACHE_WIDTH, MIN_TASK_HEIGHT + 2,
                               SDL_PIXELFORMAT_RGBA32, img,
                               MAX_CACHE_WIDTH * sizeof (Uint32));
  if (s == NULL)
    exit_with_error ("SDL_CreateRGBSurfaceFrom failed: %s", SDL_GetError ());

  for (int i = 0; i < MIN_TASK_HEIGHT + 2; i++)
    for (int j = 0; j < MAX_CACHE_WIDTH; j++)
      if (i == 0 || i == MIN_TASK_HEIGHT + 1 || j == 0 ||
          j == MAX_CACHE_WIDTH - 1)
        img[i * MAX_CACHE_WIDTH + j] = WHITE_COL;
      else
        img[i * MAX_CACHE_WIDTH + j] = BLACK_COL;

  stat_frame_tex = SDL_CreateTextureFromSurface (renderer, s);

  SDL_DestroySurface (s);
  free (img);

  s = SDL_CreateSurface (SQUARE_SIZE, SQUARE_SIZE, SDL_PIXELFORMAT_RGBA32);
  if (s == NULL)
    exit_with_error ("SDL_CreateRGBSurface () failed");

  SDL_FillSurfaceRect (s, NULL, BLACK_COL); // back
  black_square = SDL_CreateTextureFromSurface (renderer, s);

  SDL_FillSurfaceRect (s, NULL, DARK_COL); // dark
  dark_square = SDL_CreateTextureFromSurface (renderer, s);

  SDL_FillSurfaceRect (s, NULL, WHITE_COL); // white
  white_square = SDL_CreateTextureFromSurface (renderer, s);

  SDL_DestroySurface (s);
}

static void create_digit_textures (TTF_Font *font)
{
  SDL_Color white_color = {255, 255, 255, 255};
  SDL_Surface *s        = NULL;

  for (int c = 0; c < 10; c++) {
    char msg[32];
    snprintf (msg, 32, "%d", c);

    s = TTF_RenderText_Blended (font, msg, 0, white_color);
    if (s == NULL)
      exit_with_error ("TTF_RenderText_Solid failed: %s", SDL_GetError ());

    digit_tex_width[c] = s->w;
    digit_tex_height   = s->h;
    digit_tex[c]       = SDL_CreateTextureFromSurface (renderer, s);

    SDL_DestroySurface (s);
  }

  s = TTF_RenderText_Blended (font, "µs", 0, white_color);
  if (s == NULL)
    exit_with_error ("TTF_RenderText_Solid failed: %s", SDL_GetError ());

  us_tex = SDL_CreateTextureFromSurface (renderer, s);
  SDL_DestroySurface (s);

  s = TTF_RenderText_Blended (font, "Σ: ", 0, white_color);
  if (s == NULL)
    exit_with_error ("TTF_RenderText_Solid failed: %s", SDL_GetError ());

  sigma_tex = SDL_CreateTextureFromSurface (renderer, s);
  SDL_DestroySurface (s);

  s = TTF_RenderText_Blended (font, "     Stalls (%)     ", 0, silver_color);
  if (s == NULL)
    exit_with_error ("TTF_RenderText_Solid failed: %s", SDL_GetError ());

  stat_caption_tex = SDL_CreateTextureFromSurface (renderer, s);
  SDL_DestroySurface (s);
}

static void create_tab_textures (TTF_Font *font)
{
  SDL_Surface *surf = load_img ("tab-left.png");
  tab_left          = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_DestroySurface (surf);

  surf     = load_img ("tab-high.png");
  tab_high = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_DestroySurface (surf);

  surf      = load_img ("tab-right.png");
  tab_right = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_DestroySurface (surf);

  surf    = load_img ("tab-low.png");
  tab_low = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_DestroySurface (surf);

  for (int t = 0; t < nb_traces; t++) {
    SDL_Surface *s =
        TTF_RenderText_Blended (font, trace[t].label, 0, backgrd_color);
    if (s == NULL)
      exit_with_error ("TTF_RenderUTF8_Blended failed: %s", SDL_GetError ());

    trace_display_info[t].label_tex =
        SDL_CreateTextureFromSurface (renderer, s);
    trace_display_info[t].label_width  = s->w;
    trace_display_info[t].label_height = s->h;
    SDL_DestroySurface (s);
  }
}

static void create_task_ids_textures (TTF_Font *font)
{
  for (int t = 0; t < nb_traces; t++) {
    trace_display_info[t].task_ids_tex =
        calloc (trace[t].task_ids_count, sizeof (SDL_Texture *));
    trace_display_info[t].task_ids_tex_width =
        calloc (trace[t].task_ids_count, sizeof (unsigned));

    for (int i = 0; i < trace[t].task_ids_count; i++) {
      SDL_Surface *s =
          TTF_RenderText_Blended (font, trace[t].task_ids[i], 0, silver_color);
      if (s == NULL)
        exit_with_error ("TTF_RenderUTF8_Blended failed: %s", SDL_GetError ());

      trace_display_info[t].task_ids_tex[i] =
          SDL_CreateTextureFromSurface (renderer, s);
      trace_display_info[t].task_ids_tex_width[i] = s->w;
      SDL_DestroySurface (s);
    }
  }
}

static void to_sdl_color (uint32_t src, SDL_Color *dst)
{
  dst->r = ezv_c2r (src);
  dst->g = ezv_c2g (src);
  dst->b = ezv_c2b (src);
  dst->a = ezv_c2a (src);
}

static void blit_on_surface (SDL_Surface *surface, TTF_Font *font, int trace,
                             unsigned line, char *msg, unsigned color)
{
  SDL_Rect dst;
  SDL_Color col;

  to_sdl_color (color, &col);

  SDL_Surface *s = TTF_RenderText_Blended (font, msg, 0, col);
  if (s == NULL)
    exit_with_error ("TTF_RenderUTF8_Blended failed: %s", SDL_GetError ());

  dst.x = LEFT_MARGIN - s->w;
  dst.y = trace_display_info[trace].gantt.y +
          cpu_row_height (TASK_HEIGHT) * line +
          cpu_row_height (TASK_HEIGHT) / 2 - (s->h / 2);

  SDL_BlitSurface (s, NULL, surface, &dst);
  SDL_DestroySurface (s);
}

static void blit_sub_on_surface (SDL_Surface *surface, TTF_Font *font,
                                 int trace, unsigned line, unsigned color)
{
  SDL_Rect dst;
  SDL_Color col;

  to_sdl_color (color, &col);

  SDL_Surface *s = TTF_RenderText_Blended (font, "in", 0, col);
  if (s == NULL)
    exit_with_error ("TTF_RenderUTF8_Blended failed: %s", SDL_GetError ());

  dst.x = LEFT_MARGIN - s->w;
  dst.y =
      trace_display_info[trace].gantt.y + cpu_row_height (TASK_HEIGHT) * line;
  dst.h = cpu_row_height (TASK_HEIGHT) / 2 - 1;

  SDL_BlitSurface (s, NULL, surface, &dst);
  SDL_DestroySurface (s);

  s = TTF_RenderText_Blended (font, "out", 0, col);
  if (s == NULL)
    exit_with_error ("TTF_RenderUTF8_Blended failed: %s", SDL_GetError ());

  dst.x = LEFT_MARGIN - s->w;
  dst.y = trace_display_info[trace].gantt.y +
          cpu_row_height (TASK_HEIGHT) * line +
          cpu_row_height (TASK_HEIGHT) / 2;

  SDL_BlitSurface (s, NULL, surface, &dst);
  SDL_DestroySurface (s);
}

static void create_cpu_textures (TTF_Font *font)
{
  SDL_Surface *surface =
      SDL_CreateSurface (LEFT_MARGIN, WINDOW_HEIGHT, SDL_PIXELFORMAT_RGBA32);
  if (surface == NULL)
    exit_with_error ("SDL_CreateRGBSurface failed: %s", SDL_GetError ());

  for (int t = 0; t < nb_traces; t++) {
    for (int c = 0; c < trace[t].nb_cores; c++) {
      char msg[32];
      int is_lane = 0;
      if (is_gpu (trace + t, c)) {
        int lane = c - (trace[t].nb_cores - trace[t].nb_gpu);
        int gpu  = lane >> 1;
        if (lane & 1) {
          snprintf (msg, 32, "I/O        ");
          is_lane = 1;
        } else
          snprintf (msg, 32, "GPU %2d ", gpu);
      } else
        snprintf (msg, 32, "CPU %2d ", c);

      blit_on_surface (surface, font, t, c, msg, trace_cpu_color (c));
      if (is_lane) {
        blit_sub_on_surface (surface, font, t, c, trace_cpu_color (c));
        blit_sub_on_surface (surface, font, t, c, trace_cpu_color (c));
      }
    }
  }

  if (text_texture == NULL)
    SDL_DestroyTexture (text_texture);

  text_texture = SDL_CreateTextureFromSurface (renderer, surface);
  if (text_texture == NULL)
    exit_with_error ("SDL_CreateTexture failed: %s", SDL_GetError ());

  SDL_DestroySurface (surface);
}

static void create_text_texture (TTF_Font *font)
{
  create_cpu_textures (font);
  create_digit_textures (font);
  create_tab_textures (font);
  create_task_ids_textures (font);
}

static void create_misc_tex (void)
{
  SDL_Surface *surf =
      SDL_CreateSurface (2, WINDOW_HEIGHT, SDL_PIXELFORMAT_RGBA32);
  if (surf == NULL)
    exit_with_error ("SDL_CreateRGBSurface failed: %s", SDL_GetError ());

  SDL_FillSurfaceRect (
      surf, NULL,
      SDL_MapRGB (SDL_GetPixelFormatDetails (surf->format), NULL, 0, 255, 255));
  vertical_line = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_SetTextureBlendMode (vertical_line, SDL_BLENDMODE_BLEND);
  SDL_DestroySurface (surf);

  surf = SDL_CreateSurface (GANTT_WIDTH, 2, SDL_PIXELFORMAT_RGBA32);
  if (surf == NULL)
    exit_with_error ("SDL_CreateRGBSurface failed: %s", SDL_GetError ());

  SDL_FillSurfaceRect (
      surf, NULL,
      SDL_MapRGB (SDL_GetPixelFormatDetails (surf->format), NULL, 0, 255, 255));
  horizontal_line = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_SetTextureBlendMode (horizontal_line, SDL_BLENDMODE_BLEND);

  SDL_FillSurfaceRect (surf, NULL,
                       SDL_MapRGB (SDL_GetPixelFormatDetails (surf->format),
                                   NULL, 150, 150, 200));
  horizontal_bis = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_SetTextureBlendMode (horizontal_bis, SDL_BLENDMODE_BLEND);

  SDL_DestroySurface (surf);

  surf      = load_img ("mouse-cursor.png");
  mouse_tex = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_DestroySurface (surf);

  surf = load_img ("frame.png");

  BUBBLE_WIDTH  = surf->w;
  BUBBLE_HEIGHT = surf->h;

  bulle_tex = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_DestroySurface (surf);

  surf = load_img ("frame-reduced.png");

  REDUCED_BUBBLE_WIDTH  = surf->w;
  REDUCED_BUBBLE_HEIGHT = surf->h;

  reduced_bulle_tex = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_DestroySurface (surf);

  surf = load_img ("quick-nav.png");

  quick_nav_tex = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_DestroySurface (surf);
  SDL_SetTextureAlphaMod (quick_nav_tex, quick_nav_mode ? 255 : BUTTON_ALPHA);

  SDL_GetTextureSize (quick_nav_tex, &quick_nav_rect.w, &quick_nav_rect.h);

  surf = load_img ("auto-align.png");

  align_tex = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_DestroySurface (surf);
  SDL_SetTextureAlphaMod (align_tex,
                          trace_data_align_mode ? 255 : BUTTON_ALPHA);

  SDL_GetTextureSize (align_tex, &align_rect.w, &align_rect.h);

  surf = load_img ("track-mode.png");

  track_tex = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_DestroySurface (surf);
  SDL_SetTextureAlphaMod (track_tex, tracking_mode ? 255 : BUTTON_ALPHA);

  SDL_GetTextureSize (track_tex, &track_rect.w, &track_rect.h);

  surf = load_img ("footprint.png");

  footprint_tex = SDL_CreateTextureFromSurface (renderer, surf);
  SDL_DestroySurface (surf);
  SDL_SetTextureAlphaMod (footprint_tex, footprint_mode ? 255 : BUTTON_ALPHA);

  SDL_GetTextureSize (footprint_tex, &footprint_rect.w, &footprint_rect.h);
}

// Display functions

static void show_tile (trace_t *tr, trace_task_t *t, unsigned cpu,
                       unsigned highlight)
{
  uint32_t task_color = trace_cpu_color (cpu);

  if (easyview_mode == EASYVIEW_MODE_3D_MESHES) {
    if (t->w) {
      ezv_set_cpu_color_1D (
          ctx[tr->num], t->x, t->w,
          (highlight ? task_color
                     : (task_color & ezv_rgb_mask ()) | ezv_a2c (0xC0)));
    }
  } else {
    if (t->w && t->h) {
      ezv_set_cpu_color_2D (
          ctx[tr->num], t->x, t->w, t->y, t->h,
          (highlight ? task_color
                     : (task_color & ezv_rgb_mask ()) | ezv_a2c (0xC0)));
    }
  }
}

static void display_tab (unsigned trace_num)
{
  SDL_FRect dst;

  dst.x = trace_display_info[trace_num].gantt.x;
  dst.y = trace_display_info[trace_num].gantt.y - 24;
  dst.w = 32;
  dst.h = 24;
  SDL_RenderTexture (renderer, tab_left, NULL, &dst);

  dst.x += 8;
  dst.y = trace_display_info[trace_num].gantt.y - 24;
  dst.w = trace_display_info[trace_num].label_width;
  dst.h = 24;
  SDL_RenderTexture (renderer, tab_high, NULL, &dst);

  dst.y += 2;
  dst.w = trace_display_info[trace_num].label_width;
  dst.h = trace_display_info[trace_num].label_height;
  SDL_RenderTexture (renderer, trace_display_info[trace_num].label_tex, NULL,
                     &dst);

  dst.x += dst.w;
  dst.y -= 2;
  dst.w = 32;
  dst.h = 24;
  SDL_RenderTexture (renderer, tab_right, NULL, &dst);

  dst.x += 32;
  dst.w =
      trace_display_info[trace_num].gantt.w; // We don't care, thanks clipping!
  SDL_RenderTexture (renderer, tab_low, NULL, &dst);
}

static void display_text (void)
{
  SDL_FRect dst;

  dst.x = 0;
  dst.y = 0;
  dst.w = LEFT_MARGIN;
  dst.h = WINDOW_HEIGHT;

  SDL_RenderTexture (renderer, text_texture, NULL, &dst);
}

static void display_iter_number (unsigned iter, unsigned y_offset,
                                 unsigned x_offset, unsigned max_size)
{
  unsigned digits[10];
  unsigned nbd = 0, width;
  SDL_FRect dst;

  do {
    digits[nbd] = iter % 10;
    iter /= 10;
    nbd++;
  } while (iter > 0);

  width = nbd * digit_tex_width[0]; // approx

  dst.x = x_offset + max_size / 2 - width / 2;
  dst.y = y_offset;
  dst.h = digit_tex_height;

  for (int d = nbd - 1; d >= 0; d--) {
    unsigned the_digit = digits[d];
    dst.w              = digit_tex_width[the_digit];

    SDL_RenderTexture (renderer, digit_tex[the_digit], NULL, &dst);

    dst.x += digit_tex_width[the_digit];
  }
}

static void display_duration (unsigned long task_duration, unsigned x_offset,
                              unsigned y_offset, unsigned max_size,
                              unsigned with_sigma)
{
  unsigned digits[10];
  unsigned nbd = 0, width;
  SDL_FRect dst;

  do {
    digits[nbd] = task_duration % 10;
    task_duration /= 10;
    nbd++;
  } while (task_duration > 0);

  width = (nbd + 2 + 2 * with_sigma) * digit_tex_width[0]; // approx

  dst.x = x_offset + max_size / 2 - width / 2;
  dst.y = y_offset;
  dst.h = digit_tex_height;

  if (with_sigma) {
    dst.w = 19;
    SDL_RenderTexture (renderer, sigma_tex, NULL, &dst);
    dst.x += dst.w;
  }

  dst.w = digit_tex_width[0];

  for (int d = nbd - 1; d >= 0; d--) {
    unsigned the_digit = digits[d];

    SDL_RenderTexture (renderer, digit_tex[the_digit], NULL, &dst);

    dst.x += digit_tex_width[the_digit];
  }

  dst.w = 18;
  SDL_RenderTexture (renderer, us_tex, NULL, &dst);
}

static void display_selection (void)
{
  if (selection_duration > 0) {
    SDL_FRect dst;

    dst.x = time_to_pixel (selection_start_time);
    dst.y = trace_display_info[0].gantt.y;
    dst.w =
        time_to_pixel (selection_start_time + selection_duration) - dst.x + 1;
    dst.h = GANTT_HEIGHT;

    SDL_SetRenderDrawColor (renderer, 255, 255, 255, 100);
    SDL_RenderFillRect (renderer, &dst);

    display_duration (selection_duration, dst.x + dst.w / 2, dst.y + dst.h / 2,
                      0, 0);
  }
}

static void display_cache_histo (const int64_t *counters, int x, int y, int w,
                                 int h)
{
  if (counters[EASYPAP_TOTAL_CYCLES] == 0)
    return;

  // STALL RATIO
  unsigned ratio =
      counters[EASYPAP_TOTAL_STALLS] * 255 / counters[EASYPAP_TOTAL_CYCLES];
  SDL_FRect dst;

  SDL_SetRenderDrawColor (renderer, ratio, 255 - ratio, 0, 255);

  dst.x = x;
  dst.y = y;
  dst.w = counters[EASYPAP_TOTAL_STALLS] * w / counters[EASYPAP_TOTAL_CYCLES];
  dst.h = h;

  SDL_RenderFillRect (renderer, &dst);
}

static void display_bubble (int x, int y, unsigned long duration,
                            unsigned with_sigma, unsigned cache_info,
                            const int64_t *counters)
{
  SDL_FRect dst;

  if (cache_info) {
    dst.x = x - (BUBBLE_WIDTH >> 1);
    dst.y = y - BUBBLE_HEIGHT;
    dst.w = BUBBLE_WIDTH;
    dst.h = BUBBLE_HEIGHT;

    SDL_RenderTexture (renderer, bulle_tex, NULL, &dst);

    display_cache_histo (counters, dst.x + 8, dst.y + 20, 100, 8);

    display_duration (duration, x - (BUBBLE_WIDTH >> 1) + 1, dst.y + 1,
                      dst.w - 3, with_sigma);
  } else {
    dst.x = x - (REDUCED_BUBBLE_WIDTH >> 1);
    dst.y = y - REDUCED_BUBBLE_HEIGHT;
    dst.w = REDUCED_BUBBLE_WIDTH;
    dst.h = REDUCED_BUBBLE_HEIGHT;

    SDL_RenderTexture (renderer, reduced_bulle_tex, NULL, &dst);

    display_duration (duration, x - (REDUCED_BUBBLE_WIDTH >> 1) + 1, dst.y + 1,
                      dst.w - 3, with_sigma);
  }
}

typedef struct
{
  trace_task_t *task;
  trace_t *trace;
  unsigned iter;
  long cumulated_duration;
  SDL_Rect area;
  perfcounter_array_t cumulated_cstats;
} selected_task_info_t;

#define SELECTED_TASK_INFO_INITIALIZER                                         \
  {NULL,                                                                       \
   NULL,                                                                       \
   0,                                                                          \
   0,                                                                          \
   {0, 0, 0, 0},                                                               \
   {                                                                           \
       0, 0 /*, 0, 0*/                                                         \
   }}

static void display_mouse_selection (const selected_task_info_t *selected)
{
  SDL_FRect dst;

  if (horiz_mode) {
    if (!footprint_mode) {
      // horizontal bar
      if (mouse_in_gantt_zone) {
        dst.x = trace_display_info[0].gantt.x;
        dst.y = mouse.y;
        dst.w = GANTT_WIDTH;
        dst.h = 1;

        SDL_RenderTexture (renderer, horizontal_line, NULL, &dst);

        dst.y = get_y_mouse_sibbling ();
        if (dst.y != mouse.y)
          SDL_RenderTexture (renderer, horizontal_bis, NULL, &dst);
      }
    }
  } else {
    trace_t *tr     = selected->trace;
    trace_task_t *t = selected->task;

    // vertical bar
    if (mouse_in_gantt_zone) {
      dst.x = mouse.x;
      dst.w = 1;
      if (tracking_mode) {
        if (tr != NULL) {
          dst.x = mouse.x;
          dst.y = trace_display_info[tr->num].gantt.y;
          dst.w = 1;
          dst.h = trace_display_info[tr->num].gantt.h;
          SDL_RenderTexture (renderer, vertical_line, NULL, &dst);
        }
      } else {
        dst.x = mouse.x;
        dst.y = trace_display_info[0].gantt.y;
        dst.w = 1;
        dst.h = GANTT_HEIGHT;
        SDL_RenderTexture (renderer, vertical_line, NULL, &dst);
      }

      if (t != NULL) {
        display_bubble (mouse.x, trace_display_info[tr->num].gantt.y - 4,
                        t->end_time - t->start_time, 0, tr->has_cache_data,
                        t->counters);

        if (tracking_mode) {
          // int iter = selected->iter - tr->first_iteration;
          // int x = (time_to_pixel (iteration_start_time (tr, iter)) +
          //         time_to_pixel (iteration_end_time (tr, iter))) >> 1;
          int y = trace_display_info[1 - tr->num].gantt.y - 4;
          display_bubble (mouse.x, y, selected->cumulated_duration, 1,
                          trace[1 - tr->num].has_cache_data,
                          selected->cumulated_cstats);
        }

        if (t->task_id) { // Do not display "anonymous" IDs
          dst.w = trace_display_info[tr->num].task_ids_tex_width[t->task_id];
          dst.h = FONT_HEIGHT;
          dst.x = mouse.x - dst.w / 2;
          dst.y = trace_display_info[tr->num].gantt.y +
                  trace_display_info[tr->num].gantt.h;
          SDL_RenderTexture (
              renderer, trace_display_info[tr->num].task_ids_tex[t->task_id],
              NULL, &dst);
        }
      }
    }
  }
}

static void display_misc_status (void)
{
  SDL_RenderTexture (renderer, quick_nav_tex, NULL, &quick_nav_rect);
  SDL_RenderTexture (renderer, footprint_tex, NULL, &footprint_rect);

  if (nb_traces > 1) {
    SDL_RenderTexture (renderer, align_tex, NULL, &align_rect);
    SDL_RenderTexture (renderer, track_tex, NULL, &track_rect);
  }
}

static void display_tile_background (int tr)
{
  static int displayed_iter[MAX_TRACES] = {-1, -1};
  static void *tex[MAX_TRACES]          = {NULL, NULL};

  if (mouse_in_gantt_zone) {
    long time = pixel_to_time (mouse.x);
    int iter  = trace_data_search_iteration (&trace[tr], time);

    if (iter != -1 && iter != displayed_iter[tr]) {
      displayed_iter[tr] = iter;
      tex[tr]            = thumb_data[tr][iter];
    }
  }

  if (tex[tr] != NULL) {
    if (easyview_mode == EASYVIEW_MODE_2D_IMAGES)
      ezv_set_data_colors (ctx[tr], tex[tr]);
    else
      ezv_set_data_colors (ctx[tr], tex[tr]);
  }
}

static void display_gantt_background (trace_t *tr, int _t, int first_it)
{
  display_tab (_t);

  // Display iterations' background and number
  for (unsigned it = first_it;
       (it < tr->nb_iterations) && (iteration_start_time (tr, it) < end_time);
       it++) {
    SDL_Rect r;

    r.x = time_to_pixel (iteration_start_time (tr, it));
    r.y = trace_display_info[_t].gantt.y;
    r.w = time_to_pixel (iteration_end_time (tr, it)) - r.x + 1;
    r.h = trace_display_info[_t].gantt.h;

    // Background of iterations is black
    {
      SDL_Texture *ptr_tex = (it % 2 ? dark_square : black_square);

      SDL_FRect dst = {r.x, r.y, r.w, TASK_HEIGHT + 2 * Y_MARGIN};

      for (int c = 0; c < tr->nb_cores; c++) {
        SDL_RenderTexture (renderer, ptr_tex, NULL, &dst);
        dst.y += cpu_row_height (TASK_HEIGHT);
      }
    }

    if (trace_data_align_mode && tr->iteration[it].gap > 0) {
      SDL_FRect gap;

      gap.x =
          time_to_pixel (iteration_end_time (tr, it) - tr->iteration[it].gap);
      gap.y = r.y;
      gap.w = r.x + r.w - gap.x;
      gap.h = r.h;

      SDL_SetRenderDrawColor (renderer, 0, 90, 0, 255);
      SDL_RenderFillRect (renderer, &gap);
    }

    display_iter_number (tr->first_iteration + it,
                         trace_display_info[_t].gantt.y +
                             trace_display_info[_t].gantt.h + 1,
                         r.x, r.w);
  }
}

static void display_cache_stats (trace_t *tr, int _t,
                                 perfcounter_array_t *stats)
{
  // Draw cache stats background
  SDL_FRect r;

  r.x = trace_display_info[_t].gantt.x + trace_display_info[_t].gantt.w +
        RIGHT_MARGIN;
  r.w = MAX_CACHE_WIDTH;
  r.h = 18;
  r.y = trace_display_info[_t].gantt.y - r.h;

  SDL_RenderTexture (renderer, stat_caption_tex, NULL, &r);

  r.y += r.h + cpu_row_height (TASK_HEIGHT) / 2 - MIN_TASK_HEIGHT / 2;
  r.h = MIN_TASK_HEIGHT + 2;

  for (int c = 0; c < tr->nb_cores; c++) {
    SDL_RenderTexture (renderer, stat_frame_tex, NULL, &r);
    display_cache_histo (&stats[c][0], r.x + 1, r.y + 1, r.w - 2,
                         MIN_TASK_HEIGHT);
    r.y += cpu_row_height (TASK_HEIGHT);
  }
}

static void inline magnify (SDL_Rect *r)
{
  r->x -= MAGNIFICATION;
  r->y -= MAGNIFICATION;
  r->w += MAGNIFICATION * 2;
  r->h += MAGNIFICATION * 2;
}

static void trace_graphics_display_trace (unsigned _t,
                                          selected_task_info_t *selected)
{
  trace_t *const tr       = trace + _t;
  const unsigned first_it = trace_ctrl[_t].first_displayed_iter - 1;
  trace_task_t
      *to_be_emphasized[max_cores]; // FIXME: there is no need to use max_cores
                                    // any more. The number of cores within the
                                    // trace should be ok.
  unsigned wh         = trace_display_info[_t].gantt.y + Y_MARGIN;
  uint64_t mouse_time = 0;
  unsigned mouse_iter = 0;
  perfcounter_array_t cumulated_cache_stat[max_cores];
  unsigned mosaic_tile_displayed = 0;

  bzero (to_be_emphasized, max_cores * sizeof (trace_task_t *));
  if (tr->has_cache_data)
    bzero (cumulated_cache_stat, max_cores * sizeof (perfcounter_array_t));

  ezv_reset_cpu_colors (ctx[_t]);

  // Set clipping region
  {
    SDL_Rect clip = trace_display_info[0].gantt;

    // We enlarge the clipping area along the y-axis to enable display of
    // iteration numbers
    clip.y = 0;
    clip.h = WINDOW_HEIGHT;

    SDL_SetRenderClipRect (renderer, &clip);
  }

  display_gantt_background (tr, _t, first_it);

  display_tile_background (_t);

  SDL_Point virt_mouse = mouse;
  int in_mosaic        = 0;

  // Normalize (virt_mouse.x, virt_mouse.y) mouse coordinates
  if (mouse_in_gantt_zone) {
    mouse_time = pixel_to_time (mouse.x);
    mouse_iter =
        tr->first_iteration + trace_data_search_iteration (tr, mouse_time);

    if (point_inside_gantt (&mouse, _t))
      selected->trace = tr;

    if (horiz_mode && point_inside_gantt (&mouse, 1 - _t)) {
      virt_mouse.y = get_y_mouse_sibbling ();
      virt_mouse.x = -1;
    }
  } else {
    if (point_inside_mosaic (&mouse, _t)) {
      // Mouse is over our tile mosaic
      in_mosaic = 1;
    } else if ((nb_traces > 1) && point_inside_mosaic (&mouse, 1 - _t)) {
      // Mouse is over the other tile mosaic
      in_mosaic    = 1;
      virt_mouse.x = mouse.x;
      virt_mouse.y = mouse.y;
    }
  }

  // We go through the range of iterations and we display tasks & associated
  // tiles
  if (first_it < tr->nb_iterations)
    for (int c = 0; c < tr->nb_cores; c++) {
      // We get a pointer on the first task executed by
      // CPU 'c' at first displayed iteration
      trace_task_t *first = tr->iteration[first_it].first_cpu_task[c];

      if (first != NULL)
        // We follow the list of tasks, starting from this first task
        list_for_each_entry_from (trace_task_t, t, tr->per_cpu + c, first,
                                  cpu_chain)
        {
          if (task_end_time (tr, t) < start_time)
            continue;

          // We stop if we encounter a task belonging to a greater iteration
          if (task_start_time (tr, t) > end_time)
            break;

          // Ok, this task should appear on the screen
          if (tr->has_cache_data)
            for (int l = 0; l < EASYPAP_NB_COUNTERS; l++)
              cumulated_cache_stat[c][l] = t->counters[l];

          // Project the task in the Gantt chart
          SDL_Rect dst;
          dst.x = time_to_pixel (task_start_time (tr, t));
          dst.y = wh;
          dst.w = time_to_pixel (task_end_time (tr, t)) - dst.x + 1;
          dst.h = TASK_HEIGHT;

          unsigned col = c;

          // If task is a GPU tranfer lane, modify height, y-offset and color
          if (is_lane (tr, c)) {
            dst.h = TASK_HEIGHT / 2;
            if (t->task_type == TASK_TYPE_READ)
              dst.y += TASK_HEIGHT / 2;
          }

          // Check if mouse is within the bounds of the gantt zone
          if (mouse_in_gantt_zone) {
            int done = 0;

            if (point_in_yrange (&dst, virt_mouse.y)) {
              if (point_in_xrange (&dst, virt_mouse.x)) {
                // Mouse pointer is over task t
                selected->task = t;

                if (tracking_mode) {
                  selected->iter = tr->first_iteration + t->iteration;
                  get_raw_rect (t, &selected->area);
                }
                // The task is under the mouse cursor: display it a little
                // bigger!
                magnify (&dst);

                show_tile (tr, t, c, 1);
                done = 1;
              } else if (horiz_mode) {
                show_tile (tr, t, c, 0);
                done = 1;
              }
            } else if (!horiz_mode && !tracking_mode &&
                       point_in_xrange (&dst, virt_mouse.x)) {
              show_tile (tr, t, c, 0);
              done = 1;
            }

            if (footprint_mode && !done)
              show_tile (tr, t, c, 0);

            if (backlog_mode) {
              if (!done && (t->iteration + tr->first_iteration == mouse_iter) &&
                  (task_start_time (tr, t) <= mouse_time))
                show_tile (tr, t, c, 0);
            } else if (tracking_mode) {
              // If tracking mode is enabled, we highlight tasks which work on
              // tiles intersecting the working set of selected task
              if (selected->task != NULL && _t != selected->trace->num &&
                  selected->iter == t->iteration + tr->first_iteration &&
                  selected->task->task_id == t->task_id) {
                SDL_Rect r;

                get_raw_rect (t, &r);
                if (rects_do_intersect (&r, &selected->area)) {
                  selected->cumulated_duration += t->end_time - t->start_time;
                  for (int l = 0; l < EASYPAP_NB_COUNTERS; l++)
                    selected->cumulated_cstats[l] += t->counters[l];

                  col = TRACE_MAX_COLORS;
                  show_tile (tr, t, c, 0);
                }
              }
            }
          } else if (in_mosaic) {
            SDL_Rect r;

            get_raw_rect (t, &r);
            if (point_in_rect (&mouse_pick, &r)) {
              // Mouse in right window matches the footprint of current task
              if (!mosaic_tile_displayed) {
                if (easyview_mode == EASYVIEW_MODE_3D_MESHES)
                  ezv_set_cpu_color_1D (ctx[_t], t->x, t->w,
                                        ezv_rgba (0xFF, 0xFF, 0xFF, 0xC0));
                else
                  ezv_set_cpu_color_2D (ctx[_t], t->x, t->w, t->y, t->h,
                                        ezv_rgba (0xFF, 0xFF, 0xFF, 0xC0));
                mosaic_tile_displayed = 1;
              }
              // Display task a little bigger!
              magnify (&dst);
              col = TRACE_MAX_COLORS;
            }
          }

          SDL_FRect fr = rect2frect (&dst);
          SDL_RenderTexture (renderer, perf_fill[col], NULL, &fr);
        }

      wh += cpu_row_height (TASK_HEIGHT);
    }

  // Display mouse selection rectangle (if any)
  display_selection ();

  // Disable clipping region
  SDL_SetRenderClipRect (renderer, NULL);

  if (tr->has_cache_data)
    display_cache_stats (tr, _t, cumulated_cache_stat);
}

static void trace_graphics_display (void)
{
  selected_task_info_t selected = SELECTED_TASK_INFO_INITIALIZER;

  SDL_RenderClear (renderer);

  // Draw the dark grey background
  {
    SDL_FRect all = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};

    SDL_SetRenderDrawColor (renderer, backgrd_color.r, backgrd_color.g,
                            backgrd_color.b, backgrd_color.a);
    SDL_RenderFillRect (renderer, &all);
  }

  display_misc_status ();

  // Draw the text indicating CPU numbers
  display_text ();

  // Fix the loop "direction" so that the trace hovered by the
  // mouse pointer is displayed first
  if (nb_traces > 1 &&
      (point_inside_gantt (&mouse, 1) || point_inside_mosaic (&mouse, 1)))
    for (int _t = nb_traces - 1; _t != -1; _t--)
      trace_graphics_display_trace (_t, &selected);
  else
    for (int _t = 0; _t < nb_traces; _t++)
      trace_graphics_display_trace (_t, &selected);

  // Mouse
  display_mouse_selection (&selected);

  SDL_RenderPresent (renderer);

  ezv_render (ctx, nb_traces);
}

static void trace_graphics_save_screenshot (void)
{
  SDL_Rect rect;
  SDL_Surface *screen_surface = NULL;
  char filename[MAX_FILENAME];

  // Get viewport size
  SDL_GetRenderViewport (renderer, &rect);

  // Create SDL_Surface with depth of 32 bits
  screen_surface = SDL_CreateSurface (rect.w, rect.h, SDL_PIXELFORMAT_RGBA32);

  // Check if the surface is created properly
  if (screen_surface == NULL)
    exit_with_error ("Cannot create surface");

  SDL_FRect frect;
  // Display fake mouse cursor before screenshot
  frect.x = mouse.x - 3;
  frect.y = mouse.y;
  frect.w = 24;
  frect.h = 36;

  SDL_RenderTexture (renderer, mouse_tex, NULL, &frect);

  // Get data from SDL_Renderer and save them into surface
  if (!SDL_RenderReadPixels (renderer, NULL))
    exit_with_error ("Cannot read pixels from renderer");

  // append date & time to filename
  {
    time_t timer;
    struct tm *tm_info;
    timer   = time (NULL);
    tm_info = localtime (&timer);
    strftime (filename, MAX_FILENAME,
              "data/dump/screenshot-%Y_%m_%d-%H_%M_%S.png", tm_info);
  }

  // Save screenshot as PNG file
  if (IMG_SavePNG (screen_surface, filename) != 0)
    exit_with_error ("IMG_SavePNG (\"%s\") failed (%s)", filename,
                     SDL_GetError ());

  // Free memory
  SDL_DestroySurface (screen_surface);

  fprintf (stderr, "\"%s\" successfully captured\n", filename);
}

// Control of view

static void trace_graphics_toggle_vh_mode (void)
{
  horiz_mode ^= 1;

  backlog_mode = 0;
  if (tracking_mode) {
    tracking_mode = 0;
    SDL_SetTextureAlphaMod (track_tex, BUTTON_ALPHA);
  }
  if (footprint_mode) {
    footprint_mode = 0;
    SDL_SetTextureAlphaMod (footprint_tex, BUTTON_ALPHA);
  }

  trace_graphics_display ();
}

static void trace_graphics_toggle_backlog_mode (void)
{
  backlog_mode ^= 1;
  if (backlog_mode) {
    horiz_mode     = 0;
    tracking_mode  = 0;
    footprint_mode = 0;
  }
  trace_graphics_display ();
}

static void trace_graphics_toggle_footprint_mode (void)
{
  static unsigned old_horiz, old_track, old_backlog;

  footprint_mode ^= 1;
  SDL_SetTextureAlphaMod (footprint_tex, footprint_mode ? 255 : BUTTON_ALPHA);

  if (footprint_mode) {
    old_horiz     = horiz_mode;
    old_track     = tracking_mode;
    old_backlog   = backlog_mode;
    horiz_mode    = 1;
    tracking_mode = 0;
    backlog_mode  = 0;
  } else {
    horiz_mode    = old_horiz;
    tracking_mode = old_track;
    backlog_mode  = old_backlog;
  }
  SDL_SetTextureAlphaMod (track_tex, tracking_mode ? 255 : BUTTON_ALPHA);

  trace_graphics_display ();
}

static void trace_graphics_toggle_tracking_mode (void)
{
  if (nb_traces > 1) {
    tracking_mode ^= 1;
    SDL_SetTextureAlphaMod (track_tex, tracking_mode ? 255 : BUTTON_ALPHA);

    if (tracking_mode) {
      horiz_mode   = 0;
      backlog_mode = 0;

      if (footprint_mode) {
        footprint_mode = 0;
        SDL_SetTextureAlphaMod (footprint_tex, BUTTON_ALPHA);
      }
    }

    trace_graphics_display ();
  } else
    printf ("Warning: tracking mode is only available when visualizing two "
            "traces\n");
}

static void trace_graphics_set_quick_nav (int nav)
{
  quick_nav_mode = nav;

  SDL_SetTextureAlphaMod (quick_nav_tex, quick_nav_mode ? 255 : BUTTON_ALPHA);
}

static void set_bounds (long start, long end)
{
  start_time = start;
  end_time   = end;
  duration   = end_time - start_time;

  for (int t = 0; t < nb_traces; t++) {
    trace_ctrl[t].first_displayed_iter =
        trace_data_search_next_iteration (&trace[t], start_time) + 1;
    trace_ctrl[t].last_displayed_iter =
        trace_data_search_prev_iteration (&trace[t], end_time) + 1;
  }
}

static void update_bounds (void)
{
  long start, end;
  int li[2];

  if (trace_ctrl[0].first_displayed_iter > trace[0].nb_iterations)
    start = iteration_start_time (
        trace + nb_traces - 1,
        trace_ctrl[nb_traces - 1].first_displayed_iter - 1);
  else if (trace_ctrl[nb_traces - 1].first_displayed_iter >
           trace[nb_traces - 1].nb_iterations)
    start =
        iteration_start_time (trace, trace_ctrl[0].first_displayed_iter - 1);
  else
    start = min (
        iteration_start_time (trace, trace_ctrl[0].first_displayed_iter - 1),
        iteration_start_time (trace + nb_traces - 1,
                              trace_ctrl[nb_traces - 1].first_displayed_iter -
                                  1));

  if (trace_ctrl[0].last_displayed_iter > trace[0].nb_iterations)
    li[0] = trace[0].nb_iterations - 1;
  else
    li[0] = trace_ctrl[0].last_displayed_iter - 1;

  if (trace_ctrl[nb_traces - 1].last_displayed_iter >
      trace[nb_traces - 1].nb_iterations)
    li[1] = trace[nb_traces - 1].nb_iterations - 1;
  else
    li[1] = trace_ctrl[nb_traces - 1].last_displayed_iter - 1;

  end = max (iteration_end_time (trace, li[0]),
             iteration_end_time (trace + nb_traces - 1, li[1]));

  set_bounds (start, end);
}

static void set_widest_iteration_range (int first, int last)
{
  trace_ctrl[0].first_displayed_iter             = first;
  trace_ctrl[nb_traces - 1].first_displayed_iter = first;

  trace_ctrl[0].last_displayed_iter             = last;
  trace_ctrl[nb_traces - 1].last_displayed_iter = last;

  update_bounds ();
}

static void set_iteration_range (int trace_num)
{
  if (nb_traces > 1) {
    int other = 1 - trace_num;

    trace_ctrl[other].first_displayed_iter =
        trace_ctrl[trace_num].first_displayed_iter;
    trace_ctrl[other].last_displayed_iter =
        trace_ctrl[trace_num].last_displayed_iter;
  }

  start_time = iteration_start_time (
      trace + trace_num, trace_ctrl[trace_num].first_displayed_iter - 1);
  end_time = iteration_end_time (trace + trace_num,
                                 trace_ctrl[trace_num].last_displayed_iter - 1);

  duration = end_time - start_time;
}

static void trace_graphics_scroll (int delta)
{
  long start = start_time + duration * SHIFT_FACTOR * delta;
  long end   = start + duration;

  if (start < 0) {
    start = 0;
    end   = duration;
  }

  if (end > max_time) {
    end   = max_time;
    start = end - duration;
  }

  if (start != start_time || end != end_time) {
    set_bounds (start, end);
    trace_graphics_set_quick_nav (0);

    trace_graphics_display ();
  }
}

static void trace_graphics_shift_left (void)
{
  if (quick_nav_mode) {
    int longest = (trace[0].nb_iterations >= trace[nb_traces - 1].nb_iterations)
                      ? 0
                      : nb_traces - 1;

    if (trace_ctrl[longest].last_displayed_iter < max_iterations) {
      trace_ctrl[longest].first_displayed_iter++;
      trace_ctrl[longest].last_displayed_iter++;

      set_iteration_range (longest);

      trace_graphics_display ();
    }
  } else {
    trace_graphics_scroll (1);
  }
}

static void trace_graphics_shift_right (void)
{
  if (quick_nav_mode) {
    int longest = (trace[0].nb_iterations >= trace[nb_traces - 1].nb_iterations)
                      ? 0
                      : nb_traces - 1;

    if (trace_ctrl[longest].first_displayed_iter > 1) {
      trace_ctrl[longest].first_displayed_iter--;
      trace_ctrl[longest].last_displayed_iter--;

      set_iteration_range (longest);

      trace_graphics_display ();
    }
  } else {
    trace_graphics_scroll (-1);
  }
}

static void trace_graphics_zoom_in (void)
{
  if (quick_nav_mode && (trace_ctrl[0].last_displayed_iter >
                         trace_ctrl[0].first_displayed_iter)) {

    int longest = (trace[0].nb_iterations >= trace[nb_traces - 1].nb_iterations)
                      ? 0
                      : nb_traces - 1;

    trace_ctrl[longest].last_displayed_iter--;

    set_iteration_range (longest);

    trace_graphics_display ();
  } else if (end_time > start_time + MIN_DURATION) {
    long start = start_time + duration * SHIFT_FACTOR;
    long end   = end_time - duration * SHIFT_FACTOR;

    if (end < start + MIN_DURATION)
      end = start + MIN_DURATION;

    set_bounds (start, end);
    trace_graphics_set_quick_nav (0);

    trace_graphics_display ();
  }
}

static void trace_graphics_zoom_out (void)
{
  if (quick_nav_mode) {
    int longest = (trace[0].nb_iterations >= trace[nb_traces - 1].nb_iterations)
                      ? 0
                      : nb_traces - 1;

    if (trace_ctrl[longest].last_displayed_iter < max_iterations) {
      trace_ctrl[longest].last_displayed_iter++;

      set_iteration_range (longest);

      trace_graphics_display ();
    } else if (trace_ctrl[longest].first_displayed_iter > 1) {
      trace_ctrl[longest].first_displayed_iter--;

      set_iteration_range (longest);

      trace_graphics_display ();
    }
  } else {
    long start = start_time - duration * SHIFT_FACTOR;
    long end   = end_time + duration * SHIFT_FACTOR;

    if (start < 0)
      start = 0;

    if (end > max_time)
      end = max_time;

    if (start != start_time || end != end_time) {
      set_bounds (start, end);
      trace_graphics_set_quick_nav (0);

      trace_graphics_display ();
    }
  }
}

static void trace_graphics_zoom_to_selection (void)
{
  if (selection_duration > 0) {
    long start = selection_start_time;
    long end   = selection_start_time + selection_duration;

    if (selection_duration < MIN_DURATION) {
      long delta = (MIN_DURATION - selection_duration) / 2;
      if (start < delta)
        start = 0;
      else
        start -= delta;
      end = start + MIN_DURATION;
      if (end > max_time) {
        end   = max_time;
        start = end - MIN_DURATION;
      }
    }

    set_bounds (start, end);
    trace_graphics_set_quick_nav (0);

    selection_duration = 0;

    trace_graphics_display ();
  }
}

static void trace_graphics_mouse_moved (int x, int y)
{
  mouse.x = x;
  mouse.y = y;

  if (point_inside_gantts (&mouse))
    mouse_in_gantt_zone = 1;
  else
    mouse_in_gantt_zone = 0;

  if (mouse_down) {
    // Check if mouse in out-of-range on the x-axis
    if (point_in_yrange (&gantts_bounding_box, y)) {
      if (x < trace_display_info[0].gantt.x) {
        x = trace_display_info[0].gantt.x;
        trace_graphics_scroll (-1);
      } else if (x > trace_display_info[0].gantt.x +
                         trace_display_info[0].gantt.w - 1) {
        x = trace_display_info[0].gantt.x + trace_display_info[0].gantt.w - 1;
        trace_graphics_scroll (1);
      }
    }

    long new_pos = pixel_to_time (x);

    if (new_pos < mouse_orig_time) {
      selection_start_time = new_pos;
      selection_duration   = mouse_orig_time - new_pos;
    } else {
      selection_start_time = mouse_orig_time;
      selection_duration   = new_pos - mouse_orig_time;
    }
  }

  trace_graphics_display ();
}

static void trace_graphics_mouse_down (int x, int y)
{
  mouse.x = x;
  mouse.y = y;

  if (point_inside_gantts (&mouse)) {

    mouse_orig_time    = pixel_to_time (x);
    selection_duration = 0;

    mouse_down = 1;

    trace_graphics_display ();
  }
}

static void trace_graphics_mouse_up (int x, int y)
{
  mouse_down = 0;
}

void trace_graphics_setview (int first, int last)
{
  int last_disp_it, first_disp_it;

  // Check parameters and make sure iteration range is correct
  if (last < 1)
    last_disp_it = 1;
  else if (last > max_iterations)
    last_disp_it = max_iterations;
  else
    last_disp_it = last;

  if (first < 1)
    first_disp_it = 1;
  else if (first > last_disp_it)
    first_disp_it = last_disp_it;
  else
    first_disp_it = first;

  trace_graphics_set_quick_nav (trace_data_align_mode);

  set_widest_iteration_range (first_disp_it, last_disp_it);

  trace_graphics_display ();
}

static void trace_graphics_reset_zoom (void)
{
  if (trace_data_align_mode) {

    if (!quick_nav_mode) {
      int first, last;

      if (trace_ctrl[0].first_displayed_iter >
          trace_ctrl[nb_traces - 1].last_displayed_iter)
        first = trace_ctrl[0].first_displayed_iter;
      else if (trace_ctrl[nb_traces - 1].first_displayed_iter >
               trace_ctrl[0].last_displayed_iter)
        first = trace_ctrl[nb_traces - 1].first_displayed_iter;
      else
        first = min (trace_ctrl[0].first_displayed_iter,
                     trace_ctrl[nb_traces - 1].first_displayed_iter);

      last = max (trace_ctrl[0].last_displayed_iter,
                  trace_ctrl[nb_traces - 1].last_displayed_iter);

      set_widest_iteration_range (first, last);

      trace_graphics_set_quick_nav (1);

      trace_graphics_display ();
    } else {
      trace_graphics_set_quick_nav (0);

      trace_graphics_display ();
    }
  }
}

void trace_graphics_display_all (void)
{
  set_widest_iteration_range (1, max_iterations);

  trace_graphics_set_quick_nav (trace_data_align_mode);

  trace_graphics_display ();
}

static void trace_graphics_toggle_align_mode ()
{
  if (nb_traces == 1)
    return;

  trace_data_align_mode ^= 1;

  SDL_SetTextureAlphaMod (align_tex,
                          trace_data_align_mode ? 255 : BUTTON_ALPHA);

  max_time = max (iteration_end_time (trace, trace[0].nb_iterations - 1),
                  iteration_end_time (trace + nb_traces - 1,
                                      trace[nb_traces - 1].nb_iterations - 1));

  if (end_time > max_time) {
    end_time = max_time;
    duration = end_time - start_time;
  }

  set_bounds (start_time, end_time);
  trace_graphics_set_quick_nav (0);

  trace_graphics_display ();
}

static void trace_graphics_relayout (unsigned w, unsigned h)
{
  WINDOW_WIDTH  = w;
  WINDOW_HEIGHT = h;

  layout_recompute (0);
  layout_place_buttons ();

  create_cpu_textures (the_font);

  trace_graphics_display ();
}

void trace_graphics_init (unsigned width, unsigned height)
{
  find_shared_directories ();

  max_iterations =
      max (trace[0].nb_iterations, trace[nb_traces - 1].nb_iterations);
  max_cores = max (trace[0].nb_cores, trace[nb_traces - 1].nb_cores);
  max_time  = max (iteration_end_time (trace, trace[0].nb_iterations - 1),
                   iteration_end_time (trace + nb_traces - 1,
                                       trace[nb_traces - 1].nb_iterations - 1));

  easyview_mode = (trace[0].mesh_file != NULL) ? EASYVIEW_MODE_3D_MESHES
                                               : EASYVIEW_MODE_2D_IMAGES;

  if (!SDL_Init (SDL_INIT_VIDEO))
    exit_with_error ("SDL_Init");

  SDL_Rect display_bounds;
  unsigned count = 0;

  SDL_DisplayID *ids = SDL_GetDisplays (&count);
  if (ids == NULL || count < 1)
    exit_with_error ("SDL_GetDisplays failed (%s)", SDL_GetError ());
     
  if (!SDL_GetDisplayUsableBounds (ids[0], &display_bounds))
    exit_with_error ("SDL_GetDisplayUsableBounds failed (%s)", SDL_GetError ());

  SDL_free (ids);

  height = display_bounds.h;
  width -= 512;

  const unsigned min_width  = layout_get_min_width ();
  const unsigned min_height = layout_get_min_height ();

  WINDOW_WIDTH  = max (width, min_width);
  WINDOW_HEIGHT = max (height, min_height);

  if (min_height > height)
    exit_with_error ("Window height (%d) is not big enough to display so "
                     "many CPUS\n",
                     WINDOW_HEIGHT);

  WINDOW_HEIGHT = min (WINDOW_HEIGHT, layout_get_max_height ());

  layout_recompute (1);

  char wintitle[1024];

  if (nb_traces == 1)
    sprintf (wintitle, "EasyView Trace Viewer -- \"%s\"", trace[0].label);
  else
    sprintf (wintitle, "EasyView -- \"%s\" (top) VS \"%s\" (bottom)",
             trace[0].label, trace[1].label);

  SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK,
                       SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
  SDL_GL_SetAttribute ((SDL_GLAttr)SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG, 1);
#endif

  window = SDL_CreateWindow (wintitle, WINDOW_WIDTH, WINDOW_HEIGHT,
                             SDL_WINDOW_RESIZABLE);
  if (window == NULL)
    exit_with_error ("SDL_CreateWindow");
  
  SDL_SetWindowPosition (window, 0, 0);

  main_windowID = SDL_GetWindowID (window);

  SDL_SetWindowMinimumSize (window, min_width, min_height);

  renderer = SDL_CreateRenderer (window, NULL);
  if (renderer == NULL)
    exit_with_error ("SDL_CreateRenderer");

  trace_colors_init (max_cores);

  create_task_textures ();

  create_misc_tex ();

  layout_place_buttons ();

  if (TTF_Init () < 0)
    exit_with_error ("TTF_Init");

  the_font = load_font ("FreeSansBold.ttf", FONT_HEIGHT - 4);

  create_text_texture (the_font);

  unsigned nbthumbs = preload_thumbnails (max_iterations);

  SDL_SetRenderDrawBlendMode (renderer, SDL_BLENDMODE_BLEND);

  _ezv_init (easyview_ezv_dir);

  if (easyview_mode == EASYVIEW_MODE_3D_MESHES) {
    mesh3d_obj_init (&mesh);
    mesh3d_obj_load (trace[0].mesh_file, &mesh);
  } else {
    img2d_obj_init (&img2d, trace[0].dimensions, trace[0].dimensions);
  }
  int x = -1, y = -1, w = 0, offset = 0;

  SDL_GetWindowPosition (window, &x, &y);
  SDL_GetWindowSize (window, &w, NULL);

  for (int c = 0; c < nb_traces; c++) {
    if (c > 0) {
      SDL_Window *win = ezv_sdl_window (ctx[c - 1]);
      SDL_GetWindowSize (win, NULL, &offset);
      offset += 30;
    }
    ctx[c] = ezv_ctx_create (easyview_mode == EASYVIEW_MODE_3D_MESHES
                                 ? EZV_CTX_TYPE_MESH3D
                                 : EZV_CTX_TYPE_IMG2D,
                             "Tile Mapping", x + w, y + c * offset, 512, 512,
                             EZV_ENABLE_PICKING | EZV_ENABLE_CLIPPING);

    if (easyview_mode == EASYVIEW_MODE_3D_MESHES)
      ezv_mesh3d_set_mesh (ctx[c], &mesh);
    else
      ezv_img2d_set_img (ctx[c], &img2d);

    // Color cell according to CPU
    ezv_use_cpu_colors (ctx[c]);

    if (nbthumbs > 0) {
      if (easyview_mode == EASYVIEW_MODE_3D_MESHES)
        ezv_use_data_colors_predefined (ctx[c], trace[c].palette);
      else
        ezv_use_data_colors_predefined (ctx[c], EZV_PALETTE_RGBA_PASSTHROUGH);

      ezv_set_data_brightness (ctx[c], (float)brightness / 255.0f);
    }
  }

  SDL_RaiseWindow (window);
}

void trace_graphics_process_event (SDL_Event *event)
{
  int refresh, pick;
  static int shifted = 0; // event->key.keysym.mod & KMOD_SHIFT;

  if (event->wheel.windowID != main_windowID ||
      (event->type == SDL_EVENT_MOUSE_WHEEL && shifted)) {
    // event is for OpenGL tiling window(s)
    ezv_process_event (ctx, nb_traces, event, &refresh, &pick);
    if (pick) {
      if (easyview_mode == EASYVIEW_MODE_3D_MESHES)
        mouse_pick.x = ezv_perform_1D_picking (ctx, nb_traces);
      else
        ezv_perform_2D_picking (ctx, nb_traces, &mouse_pick.x, &mouse_pick.y);
    }
    if (refresh | pick)
      trace_graphics_display ();
    return;
  }

  if (event->type == SDL_EVENT_KEY_DOWN) {
    switch (event->key.key) {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
      shifted = 1;
      break;
    case SDLK_RIGHT:
      trace_graphics_shift_left ();
      break;
    case SDLK_LEFT:
      trace_graphics_shift_right ();
      break;
    case SDLK_MINUS:
    case SDLK_KP_MINUS:
    case SDLK_M:
      trace_graphics_zoom_out ();
      break;
    case SDLK_PLUS:
    case SDLK_KP_PLUS:
    case SDLK_P:
      trace_graphics_zoom_in ();
      break;
    case SDLK_SPACE:
      trace_graphics_reset_zoom ();
      break;
    case SDLK_W:
      trace_graphics_display_all ();
      break;
    case SDLK_A:
      trace_graphics_toggle_align_mode ();
      break;
    case SDLK_X:
      trace_graphics_toggle_vh_mode ();
      break;
    case SDLK_T:
      trace_graphics_toggle_tracking_mode ();
      break;
    case SDLK_F:
      trace_graphics_toggle_footprint_mode ();
      break;
    case SDLK_B:
      trace_graphics_toggle_backlog_mode ();
      break;
    case SDLK_Z:
      trace_graphics_zoom_to_selection ();
      break;
    case SDLK_S:
      trace_graphics_save_screenshot ();
      break;
    default:
      ezv_process_event (ctx, nb_traces, event, &refresh, &pick);
      if (pick) {
        if (easyview_mode == EASYVIEW_MODE_3D_MESHES)
          mouse_pick.x = ezv_perform_1D_picking (ctx, nb_traces);
        else
          ezv_perform_2D_picking (ctx, nb_traces, &mouse_pick.x, &mouse_pick.y);
      }
      if (pick | refresh)
        trace_graphics_display ();
      break;
    }
  } else if (event->type == SDL_EVENT_KEY_UP) {
    if (event->key.key == SDLK_LSHIFT || event->key.key == SDLK_RSHIFT)
      shifted = 0;
  } else if (event->type == SDL_EVENT_MOUSE_MOTION) {
    trace_graphics_mouse_moved (event->motion.x, event->motion.y);
  } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    trace_graphics_mouse_down (event->button.x, event->button.y);
  } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
    trace_graphics_mouse_up (event->button.x, event->button.y);
  } else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
    trace_graphics_scroll (event->wheel.x);
  } else if (event->type == SDL_EVENT_WINDOW_RESIZED) {
    trace_graphics_relayout (event->window.data1, event->window.data2);
  }
}

void trace_graphics_clean ()
{
  if (renderer != NULL)
    SDL_DestroyRenderer (renderer);
  else
    return;

  if (window != NULL)
    SDL_DestroyWindow (window);
  else
    return;
}