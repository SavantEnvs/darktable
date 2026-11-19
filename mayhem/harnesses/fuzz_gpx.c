// Fuzz darktable's GPX track loader (src/common/gpx.c). In darktable the user
// picks a .gpx file in the geotagging module ("apply GPX track file"); it is
// parsed with dt_gpx_new() and every selected image is then located on the
// track with dt_gpx_get_location() at the image's EXIF time.
//
// Input: bytes 0..7 are the query time (signed little-endian seconds since the
// epoch, the EXIF timestamp of the image being geotagged); the rest is the
// .gpx file, written to a temp file because dt_gpx_new() takes a path.
#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/darktable.h"
#include "common/gpx.h"

void dt_shim_init(void);

// A GLib critical is a g_return_if_fail() contract violation: darktable passed
// glib an invalid argument (e.g. a NULL GDateTime for a <trkpt> without a
// <time>). glib only logs it and returns, so libFuzzer would see a clean run;
// abort instead, so the report carries the darktable backtrace.
static void _abort_on_critical(const gchar *domain, GLogLevelFlags level, const gchar *msg, gpointer data)
{
  g_log_default_handler(domain, level, msg, data);
  if(level & (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR)) abort();
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
  dt_shim_init();
  g_log_set_default_handler(_abort_on_critical, NULL);
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if(size < 8) return -1;
  uint64_t u = 0;
  for(int i = 7; i >= 0; i--) u = (u << 8) | data[i];
  const int64_t t = (int64_t)u;

  const char *tmp = getenv("TMPDIR");
  if(!tmp || !*tmp) tmp = "/tmp";
  char path[4096];
  snprintf(path, sizeof(path), "%s/dt_gpx_XXXXXX.gpx", tmp);
  int fd = mkstemps(path, 4);
  if(fd < 0) abort();
  for(size_t off = 8; off < size;)
  {
    ssize_t n = write(fd, data + off, size - off);
    if(n <= 0) abort();
    off += n;
  }
  close(fd);

  struct dt_gpx_t *gpx = dt_gpx_new(path);
  if(gpx)
  {
    // an out-of-range time gives NULL, as for an image without a valid EXIF date
    GDateTime *ts = g_date_time_new_from_unix_utc(t);
    if(ts)
    {
      dt_image_geoloc_t geoloc = { 0 };
      dt_gpx_get_location(gpx, ts, &geoloc);
      g_date_time_unref(ts);
    }
    dt_gpx_destroy(gpx);
  }
  g_unlink(path);
  return 0;
}
