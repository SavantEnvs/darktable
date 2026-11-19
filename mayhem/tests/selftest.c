// Known-answer tests for the darktable parsers the fuzz targets cover:
// the expression calculator, the DNG opcode list parser + GainMap application,
// darktable-chart's CHT / IT8 loaders, the GPX track loader and the PNM, RGBE,
// QOI and PFM image loaders. Each check prints PASS/FAIL; the
// summary line is parsed by mayhem/test.sh.
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/darktable.h"
#include "common/image.h"
#include "common/dng_opcode.h"
#include "common/calculator.h"
#include "chart/colorchart.h"
#include "common/gpx.h"
#include "common/mipmap_cache.h"
#include "imageio/imageio_pnm.h"
#include "imageio/imageio_rgbe.h"
#include "imageio/imageio_qoi.h"
#include "imageio/imageio_pfm.h"

void dt_shim_init(void);
void dt_shim_mipmap_release(dt_mipmap_buffer_t *mbuf);

static int passed, failed;
#define CHECK(name, cond) do { if(cond) { passed++; printf("PASS %s\n", name); } \
                               else { failed++; printf("FAIL %s\n", name); } } while(0)

static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void be64d(uint8_t *p, double d) { uint64_t v; memcpy(&v, &d, 8); for(int i = 0; i < 8; i++) p[i] = v >> (56 - 8 * i); }
static void be32f(uint8_t *p, float f) { uint32_t v; memcpy(&v, &f, 4); be32(p, v); }

static int near(double a, double b) { return fabs(a - b) < 1e-6; }

static void test_calculator(void)
{
  CHECK("calc_precedence", near(dt_calculator_solve(0, "1+2*3"), 7.0));
  CHECK("calc_parens", near(dt_calculator_solve(0, "(1+2)/4"), 0.75));
  CHECK("calc_power", near(dt_calculator_solve(0, "2^10"), 1024.0));
  CHECK("calc_x", near(dt_calculator_solve(3.0, "x*2+1"), 7.0));
  CHECK("calc_unary_minus", near(dt_calculator_solve(0, "-4+1"), -3.0));
  CHECK("calc_invalid_is_nan", isnan(dt_calculator_solve(0, "1+*")));
}

// one GainMap opcode: 2x2 points, 1 plane, all gains = g, covering w x h
static size_t gain_map_opcode(uint8_t *p, uint32_t w, uint32_t h, float g)
{
  const uint32_t psize = 76 + 4 * 4;
  be32(p + 0, 9); be32(p + 4, 0x01030000); be32(p + 8, 0); be32(p + 12, psize);
  uint8_t *q = p + 16;
  be32(q + 0, 0); be32(q + 4, 0); be32(q + 8, h); be32(q + 12, w);
  be32(q + 16, 0); be32(q + 20, 3); be32(q + 24, 1); be32(q + 28, 1);
  be32(q + 32, 2); be32(q + 36, 2);
  be64d(q + 40, 1.0); be64d(q + 48, 1.0); be64d(q + 56, 0.0); be64d(q + 64, 0.0);
  be32(q + 72, 1);
  for(int i = 0; i < 4; i++) be32f(q + 76 + 4 * i, g);
  return 16 + psize;
}

static void test_dng_opcode(void)
{
  uint8_t buf[1024];
  dt_image_t img;

  // OpcodeList3 with one GainMap of 2.0
  memset(&img, 0, sizeof(img));
  img.width = img.p_width = 100; img.height = img.p_height = 50;
  be32(buf, 1);
  size_t n = 4 + gain_map_opcode(buf + 4, 100, 50, 2.0f);
  dt_dng_opcode_process_opcode_list_3(buf, n, &img);
  CHECK("dng_list3_gainmap_parsed", g_list_length(img.dng_gain_maps_opcode3) == 1 && img.exif_correction_data.dng.has_gainmap);
  const dt_dng_gain_map_t *gm = dt_dng_gain_map_opcode3(&img);
  CHECK("dng_gainmap_accepted", gm && gm->map_points_v == 2 && gm->map_points_h == 2 && gm->bottom == 50 && gm->right == 100);
  if(gm)
  {
    float in[4 * 4], out[4 * 4];
    for(int i = 0; i < 16; i++) in[i] = 0.25f;
    dt_dng_gain_map_apply(gm, &img, in, out, 2, 2, 0, 0, 50);
    CHECK("dng_gainmap_apply", near(out[0], 0.5) && near(out[1], 0.5) && near(out[2], 0.5) && near(out[3], 0.25) && near(out[12], 0.5));
  }
  else CHECK("dng_gainmap_apply", 0);
  g_list_free_full(img.dng_gain_maps_opcode3, g_free); img.dng_gain_maps_opcode3 = NULL;

  // GainMap not covering the frame is rejected by validation
  memset(&img, 0, sizeof(img));
  img.width = img.p_width = 100; img.height = img.p_height = 60;
  dt_dng_opcode_process_opcode_list_3(buf, n, &img);
  CHECK("dng_gainmap_wrong_size_rejected", g_list_length(img.dng_gain_maps_opcode3) == 1 && dt_dng_gain_map_opcode3(&img) == NULL);
  g_list_free_full(img.dng_gain_maps_opcode3, g_free); img.dng_gain_maps_opcode3 = NULL;

  // OpcodeList2: same map lands in dng_gain_maps
  memset(&img, 0, sizeof(img));
  dt_dng_opcode_process_opcode_list_2(buf, n, &img);
  CHECK("dng_list2_gainmap_parsed", g_list_length(img.dng_gain_maps) == 1 && ((dt_dng_gain_map_t *)img.dng_gain_maps->data)->map_planes == 1);
  g_list_free_full(img.dng_gain_maps, g_free); img.dng_gain_maps = NULL;

  // truncated payload: declared size beyond the buffer -> nothing parsed
  memset(&img, 0, sizeof(img));
  dt_dng_opcode_process_opcode_list_2(buf, n - 8, &img);
  CHECK("dng_truncated_rejected", img.dng_gain_maps == NULL);

  // WarpRectilinear, 1 plane
  memset(&img, 0, sizeof(img));
  memset(buf, 0, sizeof(buf));
  be32(buf, 1);
  const uint32_t wsize = 4 + 8 * 6 + 8 * 2;
  be32(buf + 4, 1); be32(buf + 8, 0x01030000); be32(buf + 12, 1); be32(buf + 16, wsize);
  uint8_t *q = buf + 20;
  be32(q, 1);
  for(int i = 0; i < 6; i++) be64d(q + 4 + 8 * i, 0.5 * (i + 1));
  be64d(q + 52, 0.25); be64d(q + 60, 0.75);
  dt_dng_opcode_process_opcode_list_3(buf, 20 + wsize, &img);
  CHECK("dng_warp_parsed", img.exif_correction_data.dng.has_warp && img.exif_correction_data.dng.planes == 1
        && near(img.exif_correction_data.dng.cwarp[0][0], 0.5) && near(img.exif_correction_data.dng.cwarp[0][5], 3.0)
        && near(img.exif_correction_data.dng.centre_warp[0], 0.25) && near(img.exif_correction_data.dng.centre_warp[1], 0.75)
        && img.exif_correction_type == CORRECTION_TYPE_DNG);
  // invalid plane count rejected
  memset(&img, 0, sizeof(img));
  be32(q, 2);
  dt_dng_opcode_process_opcode_list_3(buf, 20 + wsize, &img);
  CHECK("dng_warp_bad_planes_rejected", !img.exif_correction_data.dng.has_warp);
}

static const char *cht =
  "BOXES 13\n"
  "  F _ _ 0 0 40 0 40 30 0 30\n"
  "  D ALL ALL _ _ 40 30 0 0 0 0\n"
  "  X A D 01 03 10 10 0 0 10 10\n"
  "\n"
  "BOX_SHRINK 1.0\n"
  "\n"
  "REF_ROTATION 0.0\n"
  "\n"
  "EXPECTED LAB 2\n"
  "A01 50 0 0\n"
  "D03 100 0 0\n";

static const char *it8 =
  "IT8.7/2\n"
  "ORIGINATOR \"selftest\"\n"
  "NUMBER_OF_FIELDS 4\n"
  "BEGIN_DATA_FORMAT\n"
  "SAMPLE_ID XYZ_X XYZ_Y XYZ_Z\n"
  "END_DATA_FORMAT\n"
  "NUMBER_OF_SETS 12\n"
  "BEGIN_DATA\n"
  "A01 10.0 20.0 30.0\n"
  "B01 50.0 50.0 50.0\n"
  "C01 50.0 50.0 50.0\n"
  "D01 50.0 50.0 50.0\n"
  "A02 50.0 50.0 50.0\n"
  "B02 50.0 50.0 50.0\n"
  "C02 50.0 50.0 50.0\n"
  "D02 50.0 50.0 50.0\n"
  "A03 50.0 50.0 50.0\n"
  "B03 50.0 50.0 50.0\n"
  "C03 50.0 50.0 50.0\n"
  "D03 50.0 50.0 50.0\n"
  "END_DATA\n";

static const char *it8_short =
  "IT8.7/2\n"
  "NUMBER_OF_FIELDS 4\n"
  "BEGIN_DATA_FORMAT\n"
  "SAMPLE_ID XYZ_X XYZ_Y XYZ_Z\n"
  "END_DATA_FORMAT\n"
  "NUMBER_OF_SETS 1\n"
  "BEGIN_DATA\n"
  "A01 1 2 3\n"
  "END_DATA\n";

static void write_file(const char *path, const char *s)
{
  FILE *f = g_fopen(path, "wb"); fputs(s, f); fclose(f);
}

static void test_chart(void)
{
  char dir[] = "/tmp/dtselftestXXXXXX";
  if(!mkdtemp(dir)) { CHECK("chart_tmpdir", 0); return; }
  gchar *cp = g_build_filename(dir, "t.cht", NULL), *ip = g_build_filename(dir, "t.it8", NULL);
  write_file(cp, cht); write_file(ip, it8);

  chart_t *c = parse_cht(cp);
  CHECK("cht_parsed", c != NULL);
  if(c)
  {
    CHECK("cht_box_counts", g_hash_table_size(c->box_table) == 12 && g_hash_table_size(c->d_table) == 1 && g_list_length(c->f_list) == 1);
    CHECK("cht_bbox", near(c->bb_w, 40) && near(c->bb_h, 30) && near(c->box_shrink, 1.0));
    box_t *b = g_hash_table_lookup(c->box_table, "D03");
    CHECK("cht_box_geometry", b && near(b->p.x, 30.0 / 40) && near(b->p.y, 20.0 / 30) && near(b->w, 0.25));
    CHECK("cht_expected_lab", b && b->color_space == DT_COLORSPACE_LAB && near(b->color[0], 100.0));
    CHECK("cht_patch_set", g_hash_table_size(c->patch_sets) == 1 && g_hash_table_lookup(c->patch_sets, "A01 .. D03") != NULL);

    CHECK("it8_loaded", parse_it8(ip, c) == 1);
    box_t *a = g_hash_table_lookup(c->box_table, "A01");
    CHECK("it8_xyz_values", a && a->color_space == DT_COLORSPACE_XYZ && near(a->color[0], 10.0) && near(a->color[1], 20.0) && near(a->color[2], 30.0) && a->rgb[1] > 0.0f);
    write_file(ip, it8_short);
    CHECK("it8_missing_sample_rejected", parse_it8(ip, c) == 0);
    free_chart(c);
  }
  write_file(cp, "BOXES 2\n  X 01 04 A A 10 10 0 0 10 10\n");
  CHECK("cht_count_mismatch_rejected", parse_cht(cp) == NULL);
  g_remove(cp); g_remove(ip); g_rmdir(dir);
  g_free(cp); g_free(ip);
}

static void write_bytes(const char *path, const void *p, size_t n)
{
  FILE *f = g_fopen(path, "wb"); fwrite(p, 1, n, f); fclose(f);
}

static const char *gpx_doc =
  "<?xml version=\"1.0\"?>\n"
  "<gpx version=\"1.1\" creator=\"selftest\"><trk><trkseg>\n"
  "<trkpt lat=\"51.0\" lon=\"10.0\"><ele>200</ele><time>2020-01-01T12:00:10Z</time></trkpt>\n"
  "<trkpt lat=\"50.0\" lon=\"10.0\"><ele>100</ele><time>2020-01-01T12:00:00Z</time></trkpt>\n"
  "</trkseg></trk></gpx>\n";

static void test_gpx(void)
{
  char dir[] = "/tmp/dtselftestXXXXXX";
  if(!mkdtemp(dir)) { CHECK("gpx_tmpdir", 0); return; }
  gchar *gp = g_build_filename(dir, "t.gpx", NULL);
  write_file(gp, gpx_doc);
  struct dt_gpx_t *gpx = dt_gpx_new(gp);
  CHECK("gpx_parsed", gpx != NULL);
  if(gpx)
  {
    // the points are sorted by time, so 12:00:05 lies between 50N and 51N
    GDateTime *mid = g_date_time_new_utc(2020, 1, 1, 12, 0, 5);
    dt_image_geoloc_t g = { 0 };
    const gboolean in = dt_gpx_get_location(gpx, mid, &g);
    CHECK("gpx_interpolated", in && fabs(g.latitude - 50.5) < 1e-3 && fabs(g.longitude - 10.0) < 1e-6);
    g_date_time_unref(mid);
    GDateTime *after = g_date_time_new_utc(2020, 1, 1, 13, 0, 0);
    dt_image_geoloc_t e = { 0 };
    CHECK("gpx_after_track_clamps_to_end", !dt_gpx_get_location(gpx, after, &e) && near(e.latitude, 51.0) && near(e.elevation, 200.0));
    g_date_time_unref(after);
    dt_gpx_destroy(gpx);
  }
  write_file(gp, "<gpx><trk><trkseg></trk></gpx>\n");
  CHECK("gpx_mismatched_tag_rejected", dt_gpx_new(gp) == NULL);
  g_remove(gp); g_rmdir(dir); g_free(gp);
}

// open `bytes` as `name` with `loader`; returns the status, leaves pixels in *mbuf
static dt_imageio_retval_t load(dt_imageio_retval_t (*loader)(dt_image_t *, const char *, dt_mipmap_buffer_t *),
                                const char *name, const void *bytes, size_t n, dt_image_t *img, dt_mipmap_buffer_t *mbuf)
{
  gchar *p = g_build_filename(g_get_tmp_dir(), name, NULL);
  write_bytes(p, bytes, n);
  memset(img, 0, sizeof(*img));
  memset(mbuf, 0, sizeof(*mbuf));
  mbuf->size = DT_MIPMAP_FULL;
  const dt_imageio_retval_t r = loader(img, p, mbuf);
  g_remove(p); g_free(p);
  return r;
}

static void test_imageio(void)
{
  dt_image_t img;
  dt_mipmap_buffer_t mb;
  float *px;

  static const char ppm[] = "P6\n2 1\n255\n\xff\x00\x33\x00\x66\xcc";
  dt_imageio_retval_t r = load(dt_imageio_open_pnm, "dtselftest.ppm", ppm, sizeof(ppm) - 1, &img, &mb);
  px = (float *)mb.buf;
  CHECK("pnm_ppm_pixels", r == DT_IMAGEIO_OK && img.width == 2 && img.height == 1 && px
        && near(px[0], 1.0) && near(px[1], 0.0) && fabs(px[2] - 0.2f) < 1e-6
        && fabs(px[5] - 0.4f) < 1e-6 && fabs(px[6] - 0.8f) < 1e-6);
  dt_shim_mipmap_release(&mb);
  static const char ppm_bad[] = "P6\n0 1\n255\n";
  CHECK("pnm_zero_width_rejected", load(dt_imageio_open_pnm, "dtselftest.ppm", ppm_bad, sizeof(ppm_bad) - 1, &img, &mb) == DT_IMAGEIO_FILE_CORRUPTED);
  dt_shim_mipmap_release(&mb);

  // RGBE 1x1: (128, 64, 0) with exponent 129 -> (1.0, 0.5, 0.0)
  static const char hdr[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n\x80\x40\x00\x81";
  r = load(dt_imageio_open_rgbe, "dtselftest.hdr", hdr, sizeof(hdr) - 1, &img, &mb);
  px = (float *)mb.buf;
  CHECK("rgbe_pixel", r == DT_IMAGEIO_OK && img.width == 1 && img.height == 1 && px
        && near(px[0], 1.0) && near(px[1], 0.5) && near(px[2], 0.0));
  dt_shim_mipmap_release(&mb);

  // QOI 1x1: one QOI_OP_RGBA (255, 51, 0, 255) then the end marker
  static const uint8_t qoi[] = { 'q','o','i','f', 0,0,0,1, 0,0,0,1, 4, 0,
                                 0xff, 0xff, 0x33, 0x00, 0xff, 0,0,0,0,0,0,0,1 };
  r = load(dt_imageio_open_qoi, "dtselftest.qoi", qoi, sizeof(qoi), &img, &mb);
  px = (float *)mb.buf;
  CHECK("qoi_pixel", r == DT_IMAGEIO_OK && img.width == 1 && img.height == 1 && px
        && near(px[0], 1.0) && fabs(px[1] - 0.2f) < 1e-6 && near(px[2], 0.0) && near(px[3], 1.0));
  dt_shim_mipmap_release(&mb);
  static const uint8_t qoi_bad[] = { 'q','o','i','x', 0,0,0,1, 0,0,0,1, 4, 0, 0,0,0,0,0,0,0,1 };
  CHECK("qoi_bad_magic_rejected", load(dt_imageio_open_qoi, "dtselftest.qoi", qoi_bad, sizeof(qoi_bad), &img, &mb) == DT_IMAGEIO_FILE_CORRUPTED);
  dt_shim_mipmap_release(&mb);

  // PFM 1x1 colour, little endian (negative scale): (0.25, 2.0, 7.5)
  uint8_t pfm[64];
  const char *ph = "PF\n1 1\n-1.0\n";
  const size_t hl = strlen(ph);
  memcpy(pfm, ph, hl);
  const float v[3] = { 0.25f, 2.0f, 7.5f };
  memcpy(pfm + hl, v, sizeof(v));
  r = load(dt_imageio_open_pfm, "dtselftest.pfm", pfm, hl + sizeof(v), &img, &mb);
  px = (float *)mb.buf;
  CHECK("pfm_pixel", r == DT_IMAGEIO_OK && img.width == 1 && img.height == 1 && px
        && near(px[0], 0.25) && near(px[1], 2.0) && near(px[2], 7.5));
  dt_shim_mipmap_release(&mb);
  CHECK("pfm_not_pf_rejected", load(dt_imageio_open_pfm, "dtselftest.pfm", "P6\n1 1\n", 8, &img, &mb) == DT_IMAGEIO_FILE_CORRUPTED);
  dt_shim_mipmap_release(&mb);
}

int main(void)
{
  dt_shim_init();
  test_gpx();
  test_imageio();
  test_calculator();
  test_dng_opcode();
  test_chart();
  printf("SUMMARY passed=%d failed=%d\n", passed, failed);
  return failed ? 1 : 0;
}
