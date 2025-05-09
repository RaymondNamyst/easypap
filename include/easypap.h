#ifndef EASYPAP_IS_DEF
#define EASYPAP_IS_DEF

#ifdef __cplusplus
extern "C" {
#endif

#include "global.h"
#include "api_funcs.h"
#include "img_data.h"
#include "mesh_data.h"
#include "mesh_mgpu.h"
#include "ezp_helpers.h"
#include "arch_flags.h"
#include "debug.h"
#include "error.h"
#include "monitoring.h"
#include "ez_pthread.h"
#include "pthread_barrier.h"
#include "minmax.h"
#include "ezp_ctx.h"
#include "ezp_alloc.h"

#ifdef ENABLE_MPI
#include <mpi.h>
#endif

#ifdef __cplusplus
}
#endif

#endif
