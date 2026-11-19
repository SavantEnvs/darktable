// Fuzz darktable's built-in image loaders for the simple formats darktable
// opens from the user's photo folders: PNM (src/imageio/imageio_pnm.c), Radiance
// RGBE/HDR (imageio_rgbe.c), QOI (imageio_qoi.c) and PFM (imageio_pfm.c +
// src/common/pfm.c). Each is called the way dt_imageio_open() calls it: with a
// fresh dt_image_t, the file name, and a full-size mipmap buffer whose pixel
// memory comes from dt_mipmap_cache_alloc() (stubbed in dt_imageio_shim.c).
//
// Input: byte 0 % 4 selects the loader (0 pnm, 1 rgbe, 2 qoi, 3 pfm); the rest
// is the file, written to a temp file with that format's extension.
#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/darktable.h"
#include "common/image.h"
#include "common/mipmap_cache.h"
#include "imageio/imageio_pnm.h"
#include "imageio/imageio_rgbe.h"
#include "imageio/imageio_qoi.h"
#include "imageio/imageio_pfm.h"

void dt_shim_init(void);
void dt_shim_mipmap_release(dt_mipmap_buffer_t *mbuf);

typedef dt_imageio_retval_t (*loader_t)(dt_image_t *, const char *, dt_mipmap_buffer_t *);
static const struct { const char *ext; loader_t open; } loaders[] = {
  { ".pnm", dt_imageio_open_pnm },
  { ".hdr", dt_imageio_open_rgbe },
  { ".qoi", dt_imageio_open_qoi },
  { ".pfm", dt_imageio_open_pfm },
};

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
  dt_shim_init();
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if(size < 1) return -1;
  const int which = data[0] % 4;

  const char *tmp = getenv("TMPDIR");
  if(!tmp || !*tmp) tmp = "/tmp";
  char path[4096];
  snprintf(path, sizeof(path), "%s/dt_img_XXXXXX%s", tmp, loaders[which].ext);
  int fd = mkstemps(path, 4);
  if(fd < 0) abort();
  for(size_t off = 1; off < size;)
  {
    ssize_t n = write(fd, data + off, size - off);
    if(n <= 0) abort();
    off += n;
  }
  close(fd);

  dt_image_t img;
  memset(&img, 0, sizeof(img));
  g_strlcpy(img.filename, path, sizeof(img.filename));
  dt_mipmap_buffer_t mbuf;
  memset(&mbuf, 0, sizeof(mbuf));
  mbuf.size = DT_MIPMAP_FULL;

  loaders[which].open(&img, path, &mbuf);

  dt_shim_mipmap_release(&mbuf);
  g_unlink(path);
  return 0;
}
