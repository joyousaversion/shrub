#include "roulette.h"
#include <stdint.h>
#include <stdlib.h>

/* Expand rand's range only when the total needs more than one draw. */
static uint64_t draw_ticket(uint64_t total){
  const __uint128_t base = (__uint128_t)RAND_MAX+1;
  __uint128_t span = base;
  uint draws = 1;
  while(span < total){
    span *= base;
    draws++;
  }
  const __uint128_t limit = span-span%total;
  __uint128_t value;
  do{
    value = 0;
    for(uint i=0;i<draws;i++)
      value = value*base+(uint)rand();
  }while(value >= limit);
  return value%total;
}

int roulette(const uint* slices,uint* winners,uint spins,uint count){
  if(slices == NULL || winners == NULL || spins == 0 || count == 0)
    return -1;
  uint64_t total = 0;
  for(uint i=0;i<count;i++){
    if(UINT64_MAX-total < slices[i])
      return -1;
    total += slices[i];
  }
  if(total == 0)
    return -1;
  for(uint s=0;s<spins;s++){
    uint64_t ticket = draw_ticket(total);
    for(uint i=0;i<count;i++){
      if(ticket < slices[i]){
        winners[s] = i;
        break;
      }
      ticket -= slices[i];
    }
  }
  return 0;
}



  
