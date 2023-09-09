#include "cuda_kernels.cuh"
#include "cppdefs.h"

EXTERN __global__ void invert_kernel_cuda(unsigned *image, unsigned *alt_image, unsigned DIM) {
  unsigned index = get_index ();
  alt_image[index] = image[index] ^ 0xFFFFFF00;
}