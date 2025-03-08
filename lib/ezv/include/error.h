
#ifndef ERROR_H
#define ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/errno.h>

#define exit_with_error(format, ...)                                   \
  do                                                                   \
  {                                                                    \
    fprintf (stderr, "%s:%d: Error: " format "\n", __FILE__, __LINE__, \
            ##__VA_ARGS__);                                            \
    exit (EXIT_FAILURE);                                               \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif
