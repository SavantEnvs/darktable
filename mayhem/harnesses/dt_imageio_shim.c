// Stand-in for the one mipmap-cache entry point darktable's image loaders call
// (src/common/mipmap_cache.c: dt_mipmap_cache_alloc), so the loaders can be
// linked without the cache, the database and the GTK application. Like the
// real function it allocates one block of width * height * bpp bytes plus the
// buffer descriptor that precedes the payload (bpp as in src/develop/format.c
// dt_iop_buffer_dsc_to_bpp, which can't be linked alone), poisons the
// descriptor under ASan, returns a pointer just past it, and returns NULL when
// the allocation fails; the image loaders themselves are unmodified.
// The memory is owned by the harness and released after every input.
#include <stdlib.h>
#include "common/darktable.h"
#include "common/image.h"
#include "common/mipmap_cache.h"
#include "develop/format.h"
#include <glib.h>
#include <string.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define DT_SHIM_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define DT_SHIM_ASAN 1
#endif
#ifdef DT_SHIM_ASAN
#include <sanitizer/asan_interface.h>
#endif

// Same layout as the private dt_mipmap_buffer_dsc_t in src/common/mipmap_cache.c.
typedef struct dt_shim_mipmap_buffer_dsc_t
{
  uint32_t width;
  uint32_t height;
  float iscale;
  size_t size;
  int flags;
  dt_colorspaces_color_profile_type_t color_space;
#ifdef DT_SHIM_ASAN
  char redzone[16];
#endif
} DT_ALIGNED_ARRAY __attribute__((packed)) dt_shim_mipmap_buffer_dsc_t;

static void _shim_free(dt_mipmap_buffer_t *buf)
{
  if(!buf->buf) return;
  dt_shim_mipmap_buffer_dsc_t *dsc = (dt_shim_mipmap_buffer_dsc_t *)buf->buf - 1;
#ifdef DT_SHIM_ASAN
  ASAN_UNPOISON_MEMORY_REGION(dsc, sizeof(*dsc));
#endif
  free(dsc);
  buf->buf = NULL;
}

void *dt_mipmap_cache_alloc(dt_mipmap_buffer_t *buf, const dt_image_t *img)
{
  const size_t bpp = img->buf_dsc.channels
                     * (img->buf_dsc.datatype == TYPE_UINT16 ? sizeof(uint16_t) : sizeof(float));
  dt_shim_mipmap_buffer_dsc_t *dsc = NULL;
  const size_t buffer_size = (size_t)img->width * img->height * bpp + sizeof(*dsc);
  _shim_free(buf);
  dsc = dt_alloc_aligned(buffer_size);
  if(!dsc) return NULL;
  dsc->size = buffer_size;
  dsc->width = img->width;
  dsc->height = img->height;
  dsc->iscale = 1.0f;
  dsc->color_space = DT_COLORSPACE_NONE;
  dsc->flags = 1; // DT_MIPMAP_BUFFER_DSC_FLAG_GENERATE
  buf->buf = (uint8_t *)(dsc + 1);
  buf->width = img->width;
  buf->height = img->height;
  buf->iscale = 1.0f;
#ifdef DT_SHIM_ASAN
  ASAN_POISON_MEMORY_REGION(dsc, sizeof(*dsc));
#endif
  return dsc + 1;
}

void dt_shim_mipmap_release(dt_mipmap_buffer_t *buf)
{
  _shim_free(buf);
}

// darktable.c's dt_alloc_aligned (the non-Windows, non-_DEBUG branch):
// cache-line aligned, size rounded up to the alignment, released with free()
// (darktable.h maps dt_free_align to free in that configuration).
void *dt_alloc_aligned(const size_t size)
{
  const size_t alignment = DT_CACHELINE_BYTES;
  const size_t aligned_size = (size + alignment - 1) / alignment * alignment;
  void *ptr = NULL;
  if(posix_memalign(&ptr, alignment, aligned_size)) return NULL;
  return ptr;
}

// src/common/utility.c's dt_util_normalize_path, its non-Windows branch for
// the absolute paths the harness passes (utility.c itself needs darktable's
// config and file-location subsystems): a file:// URI becomes its path, any
// other absolute path is returned as a copy, a relative one is rejected.
gchar *dt_util_normalize_path(const gchar *input)
{
  gchar *filename = g_filename_from_uri(input, NULL, NULL);
  if(!filename)
  {
    if(g_str_has_prefix(input, "file://"))
      filename = g_uri_unescape_string(input + strlen("file://"), NULL);
    else
      filename = g_strdup(input);
  }
  if(filename && !g_path_is_absolute(filename))
  {
    g_free(filename);
    return NULL;
  }
  return filename;
}
