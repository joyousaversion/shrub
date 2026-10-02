#ifndef ROULETTE_H
#define ROULETTE_H
#include "typedefs.h"
/* Invalid pointers, zero sizes, or zero total weight return -1 unchanged. */
int roulette (const uint* slices,uint*winners,uint spins,uint count);
#endif
