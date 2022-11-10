#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "threads/fixed-point.h"

int fp_add_fp(int f1, int f2) {
  return f1 + f2;
}

int fp_add_int(int f, int i) {
  return f + i*FRACTION_SHIFT;
}

int fp_mul_fp(int f, int i) {
  return (int) ((int64_t) f*i / FRACTION_SHIFT);
}

int fp_mul_int(int f, int i) {
  return (int) ((int64_t) f*i);
}

int fp_sub_fp(int f1, int f2) {
  return f1 - f2;
}

int fp_sub_int(int f, int i) {
  return f - i*FRACTION_SHIFT;
}

int fp_div_fp(int f1, int f2) {
  return (int) ((int64_t) f1*FRACTION_SHIFT / f2 );
}

int fp_div_int(int f, int i) {
  return f / i;
}