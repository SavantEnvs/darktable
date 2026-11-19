// Fuzz darktable's expression evaluator (src/common/calculator.c), which
// parses every numeric value typed into a bauhaus slider / config entry,
// e.g. "1/3*x+2^-2".
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "common/calculator.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if(size < 1) return -1;
  // byte 0 picks the current slider value `x` the formula may reference
  const double x = (double)(int8_t)data[0] / 4.0;
  char *formula = malloc(size);
  memcpy(formula, data + 1, size - 1);
  formula[size - 1] = '\0';   // the API takes a C string
  volatile double r = dt_calculator_solve(x, formula);
  (void)r;
  free(formula);
  return 0;
}
