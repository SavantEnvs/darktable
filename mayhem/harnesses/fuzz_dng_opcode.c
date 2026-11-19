// Fuzz darktable's DNG OpcodeList2/OpcodeList3 parser (src/common/dng_opcode.c),
// the code exif.cc hands the raw OpcodeList2/3 tag payloads of every DNG to
// (_check_dng_opcodes), and the GainMap validation + application that
// rawprepare/demosaic run on the parsed result.
//
// Input layout: byte 0 bit 0 selects OpcodeList2 (0) or OpcodeList3 (1);
// bytes 1-2 / 3-4 are the image's active width / height (big endian, like the
// DNG data); the rest is the opcode list exactly as stored in the tag.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "common/darktable.h"
#include "common/image.h"
#include "common/dng_opcode.h"

void dt_shim_init(void);

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
  dt_shim_init();
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if(size < 5 || size > UINT32_MAX) return -1;
  const int list3 = data[0] & 1;
  const int w = (data[1] << 8) | data[2];
  const int h = (data[3] << 8) | data[4];
  data += 5;
  size -= 5;

  // exif.cc passes a heap copy of the tag's bytes (exiv2 copyTo)
  uint8_t *buf = malloc(size ? size : 1);
  if(size) memcpy(buf, data, size);

  dt_image_t img;
  memset(&img, 0, sizeof(img));
  img.width = img.p_width = w;
  img.height = img.p_height = h;

  if(list3)
  {
    dt_dng_opcode_process_opcode_list_3(buf, (uint32_t)size, &img);
    const dt_dng_gain_map_t *gm = dt_dng_gain_map_opcode3(&img);
    if(gm && w > 0 && h > 0)
    {
      // apply the accepted map over a small 8x8 RGBA tile spanning the frame
      enum { T = 8 };
      float in[4 * T * T], out[4 * T * T];
      for(int i = 0; i < 4 * T * T; i++) in[i] = 0.5f;
      const float step = (float)(w > h ? w : h) / T;
      dt_dng_gain_map_apply(gm, &img, in, out, T, T, 0.0f, 0.0f, step);
    }
  }
  else
  {
    dt_dng_opcode_process_opcode_list_2(buf, (uint32_t)size, &img);
  }

  g_list_free_full(img.dng_gain_maps, g_free);
  g_list_free_full(img.dng_gain_maps_opcode3, g_free);
  free(buf);
  return 0;
}
