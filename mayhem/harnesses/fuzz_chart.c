// Fuzz darktable-chart's reference-chart loaders (src/chart/colorchart.c):
// parse_cht() reads an Argyll .cht chart layout, parse_it8() reads the
// IT8.7/CGATS reference values for it (via lcms2) and maps them onto the
// chart's patches. Both files are user-supplied in darktable-chart.
//
// Input: byte 0 bit 0 selects the mode.
//   0 -> the rest is a .cht file, parsed with parse_cht().
//   1 -> the rest is an IT8/CGATS file, loaded with parse_it8() against a
//        fixed 3x4 chart layout (re-parsed fresh for every input).
#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "chart/colorchart.h"

static const char base_cht[] =
  "BOXES 13\n"
  "  F _ _ 0 0 40 0 40 30 0 30\n"
  "  D ALL ALL _ _ 40 30 0 0 0 0\n"
  "  X A D 01 03 10 10 0 0 10 10\n"
  "\n"
  "BOX_SHRINK 1.0\n"
  "\n"
  "REF_ROTATION 0.0\n";

static char base_path[4096];

static int write_tmp(const char *suffix, const uint8_t *data, size_t size, char *out, size_t outsz)
{
  const char *tmp = getenv("TMPDIR");
  if(!tmp || !*tmp) tmp = "/tmp";
  snprintf(out, outsz, "%s/dt_chart_XXXXXX%s", tmp, suffix);
  int fd = mkstemps(out, strlen(suffix));
  if(fd < 0) return -1;
  size_t off = 0;
  while(off < size)
  {
    ssize_t n = write(fd, data + off, size - off);
    if(n <= 0) { close(fd); g_unlink(out); return -1; }
    off += n;
  }
  close(fd);
  return 0;
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
  // a fixed, read-only chart layout shared by every IT8 input
  if(write_tmp(".cht", (const uint8_t *)base_cht, sizeof(base_cht) - 1, base_path, sizeof(base_path)))
    abort();
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if(size < 1) return -1;
  const int it8 = data[0] & 1;
  char path[4096];
  if(write_tmp(it8 ? ".it8" : ".cht", data + 1, size - 1, path, sizeof(path))) abort();

  if(it8)
  {
    chart_t *chart = parse_cht(base_path);
    if(!chart) abort();   // the fixed layout always parses
    parse_it8(path, chart);
    free_chart(chart);
  }
  else
  {
    chart_t *chart = parse_cht(path);
    if(chart) free_chart(chart);
  }
  g_unlink(path);
  return 0;
}
