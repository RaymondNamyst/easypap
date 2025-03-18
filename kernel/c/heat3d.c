
#include "easypap.h"

#include <omp.h>

static int debug_hud = -1;

void heat3d_config (char *param)
{
  if (easypap_mesh_file == NULL)
    exit_with_error ("kernel %s needs a mesh (use --load-mesh <filename>)",
                     kernel_name);

  // Choose color palette
  mesh_data_set_palette_predefined (EZV_PALETTE_HEAT);

  if (picking_enabled) {
    debug_hud = ezv_hud_alloc (ctx[0]);
    ezv_hud_on (ctx[0], debug_hud);
  }
}

void heat3d_debug (int cell)
{
  if (cell == -1)
    ezv_hud_set (ctx[0], debug_hud, "No selection");
  else
    ezv_hud_set (ctx[0], debug_hud, "Temp: %f", cur_data(cell));
}

void heat3d_init (void)
{
  PRINT_DEBUG ('u', "Mesh size: %d\n", NB_CELLS);
  PRINT_DEBUG ('u', "#Patches: %d\n", NB_PATCHES);
  PRINT_DEBUG ('u', "Min cell neighbors: %d\n", min_neighbors ());
  PRINT_DEBUG ('u', "Max cell neighbors: %d\n", max_neighbors ());
}

// The Mesh is a one-dimension array of cells of size NB_CELLS. Each cell value
// is of type 'float' and should be kept between 0.0 and 1.0.

int heat3d_do_patch_default (int start_cell, int end_cell)
{
  for (int c = start_cell; c < end_cell; c++) {
    for (int n = neighbor_start (c); n < neighbor_end (c); n++)
      // TODO
      ;
  }

  return 0;
}

///////////////////////////// Simple sequential version (seq)
// Suggested cmdline(s):
// ./run -lm 2-torus.cgns -k heat3d -v seq -a 10
//
unsigned heat3d_compute_seq (unsigned nb_iter)
{
  for (unsigned it = 1; it <= nb_iter; it++) {

    for (int p = 0; p < NB_PATCHES; p++)
      do_patch (p);

    swap_data ();
  }

  return 0;
}


///////////////////////////// Initial configuration

void heat3d_draw_random (void)
{
  int nb_spots, spot_size;

  if (NB_CELLS >= 100)
    nb_spots = NB_CELLS / 100;
  else
    nb_spots = 1;
  spot_size = NB_CELLS / nb_spots / 4;
  if (!spot_size)
    spot_size = 1;

  for (int s = 0; s < nb_spots; s++) {
    int cell = random () % (NB_CELLS - spot_size);

    for (int c = cell; c < cell + spot_size; c++)
      cur_data (c) = 1.0;
  }
}

void heat3d_draw_fifty (void)
{
  for (int c = 0; c < NB_CELLS >> 1; c++)
    cur_data (c) = 1.0;
}

static void draw_coord (int coord)
{
  const float mid = (easypap_mesh_desc.bbox.min[coord] + easypap_mesh_desc.bbox.max[coord]) / 2.0f;

  for (int c = 0; c < NB_CELLS; c++) {
    bbox_t box;
    mesh3d_obj_get_bbox_of_cell (&easypap_mesh_desc, c, &box);
    float f   = (box.min[coord] + box.max[coord]) / 2.0f;
    cur_data (c) = (f <= mid) ? 0.0f : 1.0f;
  }
}

void heat3d_draw_x (void)
{
  draw_coord (0);
}

void heat3d_draw_y (void)
{
  draw_coord (1);
}

void heat3d_draw_z (void)
{
  draw_coord (2);
}

void heat3d_draw (char *param)
{
  // Call function ${kernel}_draw_${param}, or default function (second
  // parameter) if symbol not found
  hooks_draw_helper (param, heat3d_draw_random);
}

///////////////////////////// naive OpenCL
#ifdef ENABLE_OPENCL

static cl_mem neighbors_buffer = 0, index_buffer = 0;

void heat3d_init_ocl_naive (void)
{
  cl_int err;

  // Array of all neighbors
  const int sizen = easypap_mesh_desc.total_neighbors * sizeof (unsigned);

  neighbors_buffer =
      clCreateBuffer (context, CL_MEM_READ_WRITE, sizen, NULL, NULL);
  if (!neighbors_buffer)
    exit_with_error ("Failed to allocate neighbor buffer");

  err =
      clEnqueueWriteBuffer (ocl_queue (0), neighbors_buffer, CL_TRUE, 0, sizen,
                            easypap_mesh_desc.neighbors, 0, NULL, NULL);
  check (err, "Failed to write to neighbor buffer");

  // indexes
  const int sizei = (NB_CELLS + 1) * sizeof (unsigned);

  index_buffer = clCreateBuffer (context, CL_MEM_READ_WRITE, sizei, NULL, NULL);
  if (!index_buffer)
    exit_with_error ("Failed to allocate index buffer");

  err = clEnqueueWriteBuffer (ocl_queue (0), index_buffer, CL_TRUE, 0, sizei,
                              easypap_mesh_desc.index_first_neighbor, 0, NULL,
                              NULL);
  check (err, "Failed to write to index buffer");
}

unsigned heat3d_compute_ocl_naive (unsigned nb_iter)
{
  size_t global[1] = {GPU_SIZE}; // global domain size for our calculation
  size_t local[1]  = {TILE};     // local domain size for our calculation
  cl_int err;

  monitoring_start (easypap_gpu_lane (0));

  for (unsigned it = 1; it <= nb_iter; it++) {

    // Set kernel arguments
    //
    err = 0;
    err |= clSetKernelArg (ocl_compute_kernel (0), 0, sizeof (cl_mem),
                           &ocl_cur_buffer (0));
    err |= clSetKernelArg (ocl_compute_kernel (0), 1, sizeof (cl_mem),
                           &ocl_next_buffer (0));
    err |= clSetKernelArg (ocl_compute_kernel (0), 2, sizeof (cl_mem),
                           &neighbors_buffer);
    err |= clSetKernelArg (ocl_compute_kernel (0), 3, sizeof (cl_mem),
                           &index_buffer);
    check (err, "Failed to set kernel arguments");

    err = clEnqueueNDRangeKernel (ocl_queue (0), ocl_compute_kernel (0), 1,
                                  NULL, global, local, 0, NULL, NULL);
    check (err, "Failed to execute kernel");

    // Swap buffers
    {
      cl_mem tmp          = ocl_cur_buffer (0);
      ocl_cur_buffer (0)  = ocl_next_buffer (0);
      ocl_next_buffer (0) = tmp;
    }
  }

  clFinish (ocl_queue (0));

  monitoring_end_tile (0, 0, NB_CELLS, 0, easypap_gpu_lane (0));

  return 0;
}

#endif