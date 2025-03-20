
#include "ezv_sdl_gl.h"
#include "error.h"
#include "trace_data.h"
#include "trace_file.h"
#include "trace_graphics.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

static int WINDOW_PREFERRED_WIDTH  = 1920;
static int WINDOW_PREFERRED_HEIGHT = 1024;

static int first_iteration = -1;
static int last_iteration  = -1;
static int whole_trace     = 0;

static unsigned nb_dir      = 0;
char *trace_dir[MAX_TRACES] = {NULL, NULL};

static void usage (char *progname, int val)
{
  fprintf (stderr, "Usage: %s [options] {file ...}\n", progname);
  fprintf (stderr, "options can be:\n");

  fprintf (stderr, "\t-a\t| --align\t\t: align iterations\n");
  fprintf (stderr, "\t-c\t| --compare\t\t: compare last two traces\n");
  fprintf (stderr, "\t-d\t| --dir <dir>\t\t: specify trace directory\n");
  fprintf (stderr, "\t-h\t| --help\t\t: display help\n");
  fprintf (stderr, "\t-i\t| --iteration <i>\t: display iteration i\n");
  fprintf (stderr, "\t-nt\t| --no-thumb\t\t: ignore thumbnails\n");
  fprintf (stderr, "\t-p\t| --params\t\t: use options from params.txt file\n");
  fprintf (stderr,
           "\t-r\t| --range <i> <j>\t: display iteration range [i-j]\n");
  fprintf (stderr, "\t-w\t| --whole-trace\t\t: display all iterations\n");

  exit (val);
}

static char **filter_args (int *argc, char *argv[])
{
  char *progname = "./view"; // argv [0];

  (*argc)--;
  argv++;

  while (*argc > 0) {
    if (!strcmp (*argv, "--align") || !strcmp (*argv, "-a")) {
      trace_data_align_mode = 1;
    } else if (!strcmp (*argv, "--brightness") || !strcmp (*argv, "-b")) {
      if (*argc <= 1) {
        fprintf (stderr, "Error: parameter (number) missing\n");
        usage (progname, 1);
      }
      (*argc)--;
      argv++;
      brightness = atoi (*argv);
    } else if (!strcmp (*argv, "--whole-trace") || !strcmp (*argv, "-w")) {
      whole_trace = 1;
    } else if (!strcmp (*argv, "--help") || !strcmp (*argv, "-h")) {
      usage (progname, 0);
    } else if (!strcmp (*argv, "--range") || !strcmp (*argv, "-r")) {
      if (*argc <= 2) {
        fprintf (stderr, "Error: parameter (number) missing\n");
        usage (progname, 1);
      }
      (*argc)--;
      argv++;
      first_iteration = atoi (*argv);
      (*argc)--;
      argv++;
      last_iteration = atoi (*argv);
    } else if (!strcmp (*argv, "--iteration") || !strcmp (*argv, "-i")) {
      if (*argc <= 1) {
        fprintf (stderr, "Error: parameter (number) missing\n");
        usage (progname, 1);
      }
      (*argc)--;
      argv++;
      first_iteration = last_iteration = atoi (*argv);
    } else if (!strcmp (*argv, "--dir") || !strcmp (*argv, "-d")) {
      if (*argc <= 1) {
        fprintf (stderr, "Error: parameter (dirname) missing\n");
        usage (progname, 1);
      }
      (*argc)--;
      argv++;
      if (nb_dir == MAX_TRACES)
        exit_with_error ("Cannot not specify more than two traces directories");

      trace_dir[nb_dir++] = *argv;
    } else {
      break;
    }

    (*argc)--;
    argv++;
  }

  if (trace_dir[0] == NULL)
    trace_dir[0] = "data/traces";

  return argv;
}

static inline int get_event (SDL_Event *event, int blocking)
{
  return blocking ? SDL_WaitEvent (event) : SDL_PollEvent (event);
}

static unsigned skipped_events = 0;

static int clever_get_event (SDL_Event *event)
{
  int r;
  static bool prefetched = false;
  static SDL_Event pr_event; // prefetched event

  if (prefetched) {
    *event     = pr_event;
    prefetched = false;
    return 1;
  }

  r = get_event (event, true);

  if (r != 1)
    return r;

  // check if successive, similar events can be dropped
  if (event->type == SDL_EVENT_MOUSE_MOTION) {

    do {
      int ret_code = get_event (&pr_event, false);
      if (ret_code == 1) {
        if (pr_event.type == SDL_EVENT_MOUSE_MOTION) {
          *event     = pr_event;
          prefetched = false;
          skipped_events++;
        } else {
          prefetched = true;
        }
      } else
        return 1;
    } while (prefetched == false);
  }

  return 1;
}

static void check_consistency (void)
{
  if (nb_traces == 2) {
    if (trace[0].mesh_file == NULL) {
      if (trace[1].mesh_file != NULL)
        exit_with_error ("Comparison between incompatible traces (w mesh vs w/o mesh)");
    } else {
      if (trace[1].mesh_file == NULL)
        exit_with_error ("Comparison between incompatible traces (w mesh vs w/o mesh)");
      if (strcmp(trace[0].mesh_file, trace[1].mesh_file))
        exit_with_error ("Comparison between traces operating on different meshes is not possible");
    }
  }
}

int main (int argc, char **argv)
{
  argv = filter_args (&argc, argv);

  switch (argc) {
  case 0: {
    char file[1024];

    sprintf (file, "%s/%s", trace_dir[0], "ezv_trace_current.evt");
    trace_file_load (file);

    if (trace_dir[1] != NULL) {
      sprintf (file, "%s/%s", trace_dir[1], "ezv_trace_current.evt");
      trace_file_load (file);
    }

    break;
  }
  case 1: {
    trace_file_load (argv[0]);
    break;
  }
  case 2: {
    trace_file_load (argv[0]);
    trace_file_load (argv[1]);
    break;
  }
  default:
    exit_with_error ("Too many trace files specified (max %d)", MAX_TRACES);
  }

  check_consistency ();
  
  trace_data_sync_iterations ();

  trace_graphics_init (WINDOW_PREFERRED_WIDTH, WINDOW_PREFERRED_HEIGHT);

  if (whole_trace)
    trace_graphics_display_all ();
  else
    trace_graphics_setview (first_iteration, last_iteration);

  SDL_Event event;
  bool quit = false;

  do {
    int r = clever_get_event (&event);

    if (r > 0) {

      if (event.type == SDL_EVENT_KEY_DOWN) {
        switch (event.key.key) {
        case SDLK_ESCAPE:
        case SDLK_Q:
          quit = true;
          break;
        default:;
        }
      } else if (event.type == SDL_EVENT_QUIT) {
        quit = true;
      } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        quit = true;
        break;
      }

      if (!quit) 
        trace_graphics_process_event (&event);
    }
  } while (!quit);

  // printf ("Events skipped: %u\n", skipped_events);

  trace_graphics_clean ();

  SDL_Quit ();

  return EXIT_SUCCESS;
}
