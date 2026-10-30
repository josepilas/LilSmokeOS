/*
 * lilsmoke-fbtest — J1 framebuffer smoke test
 *
 * Opens /dev/fb0, mmaps it, paints the LilSmokeOS logo in the center,
 * prints diagnostics, waits 10 seconds, exits.
 *
 * Build:  $(CC) -O2 -Wall -s lilsmoke-fbtest.c -o lilsmoke-fbtest -lpng -lz -lm
 * Run:    lilsmoke-fbtest [path/to/logo.png]
 *
 * No SDL2, no DirectFB. Just the raw Linux framebuffer API. This is the
 * simplest possible thing that could prove the fbdev kernel driver works
 * before we stack anything heavier on top.
 *
 * If the framebuffer device reports a 32-bit BGRA format (which is what
 * the SF3000/GB350 stock firmware uses), we use it directly. Otherwise
 * we fall back to converting from RGBA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <png.h>
#include <time.h>

#define LOGO_DEFAULT_PATH "/usr/share/lilsmoke/logo.png"
#define LOGO_FALLBACK_W   320
#define LOGO_FALLBACK_H   132
#define DISPLAY_SECONDS   10

/* LilSmokeOS palette */
#define COL_BG_GREY     0x2B2D31
#define COL_BG_GREEN    0x1F3D2B
#define COL_BG_BLUE     0x1E2A47
#define COL_FG_BEIGE    0xD9C9A3

/* ----------------------------------------------------------------------- */
/* helpers                                                                  */
/* ----------------------------------------------------------------------- */

static void sleep_ms(unsigned ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static uint32_t make_pixel(uint32_t r, uint32_t g, uint32_t b, int bpp)
{
    if (bpp == 32) {
        /* BGRA32 — matches SF3000/GB350 stock format */
        return 0xFF000000u | (b << 16) | (g << 8) | r;
    } else if (bpp == 16) {
        /* RGB565 */
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
    return 0;
}

/* ----------------------------------------------------------------------- */
/* PNG loading — returns malloc'd RGBA buffer or NULL on failure           */
/* ----------------------------------------------------------------------- */

static uint8_t *load_png_rgba(const char *path, int *out_w, int *out_h)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    unsigned char sig[8];
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8)) {
        fclose(fp);
        return NULL;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return NULL; }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return NULL;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    int w = png_get_image_width(png, info);
    int h = png_get_image_height(png, info);
    png_byte color = png_get_color_type(png, info);
    png_byte depth = png_get_bit_depth(png, info);

    /* Normalize to 8-bit RGBA */
    if (depth == 16) png_set_strip_16(png);
    if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (!(color & PNG_COLOR_MASK_ALPHA)) png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    size_t rowbytes = png_get_rowbytes(png, info);
    uint8_t *buf = malloc(rowbytes * h);
    if (!buf) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    png_bytep *rows = malloc(sizeof(png_bytep) * h);
    if (!rows) {
        free(buf);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }
    for (int y = 0; y < h; y++)
        rows[y] = buf + y * rowbytes;

    png_read_image(png, rows);
    png_destroy_read_struct(&png, &info, NULL);
    free(rows);
    fclose(fp);

    *out_w = w;
    *out_h = h;
    return buf;
}

/* ----------------------------------------------------------------------- */
/* fallback logo — procedurally drawn "LilSmoke" text-style block          */
/* ----------------------------------------------------------------------- */

static void draw_fallback(uint8_t *rgba, int w, int h)
{
    /* Fill with dark grey background */
    for (int i = 0; i < w * h; i++) {
        rgba[i*4 + 0] = (COL_BG_GREY >> 16) & 0xFF;  /* R */
        rgba[i*4 + 1] = (COL_BG_GREY >> 8) & 0xFF;   /* G */
        rgba[i*4 + 2] = (COL_BG_GREY) & 0xFF;        /* B */
        rgba[i*4 + 3] = 0xFF;
    }
    /* Draw a simple "L" shape in beige */
    int bx = w / 4, by = h / 4;
    int bw = w / 8, bh = h / 2;
    for (int y = by; y < by + bh; y++) {
        for (int x = bx; x < bx + bw; x++) {
            if (y < h && x < w) {
                rgba[(y*w + x)*4 + 0] = (COL_FG_BEIGE >> 16) & 0xFF;
                rgba[(y*w + x)*4 + 1] = (COL_FG_BEIGE >> 8) & 0xFF;
                rgba[(y*w + x)*4 + 2] = (COL_FG_BEIGE) & 0xFF;
                rgba[(y*w + x)*4 + 3] = 0xFF;
            }
        }
    }
    /* Bottom horizontal bar of the L */
    for (int y = by + bh - bw; y < by + bh; y++) {
        for (int x = bx; x < bx + bw * 3; x++) {
            if (y < h && x < w) {
                rgba[(y*w + x)*4 + 0] = (COL_FG_BEIGE >> 16) & 0xFF;
                rgba[(y*w + x)*4 + 1] = (COL_FG_BEIGE >> 8) & 0xFF;
                rgba[(y*w + x)*4 + 2] = (COL_FG_BEIGE) & 0xFF;
                rgba[(y*w + x)*4 + 3] = 0xFF;
            }
        }
    }
}

/* ----------------------------------------------------------------------- */
/* main                                                                     */
/* ----------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    const char *logo_path = (argc > 1) ? argv[1] : LOGO_DEFAULT_PATH;

    printf("[lilsmoke-fbtest] J1 framebuffer smoke test\n");
    printf("[lilsmoke-fbtest] logo: %s\n", logo_path);

    /* --- 1. Open /dev/fb0 --- */
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[lilsmoke-fbtest] cannot open /dev/fb0: %s\n", strerror(errno));
        fprintf(stderr, "[lilsmoke-fbtest] (this is expected in QEMU if no -display fbdev)\n");
        return 1;
    }

    /* --- 2. Query fixed + variable screen info --- */
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("[lilsmoke-fbtest] FBIOGET_FSCREENINFO");
        close(fd);
        return 1;
    }
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("[lilsmoke-fbtest] FBIOGET_VSCREENINFO");
        close(fd);
        return 1;
    }

    int screen_w = vinfo.xres;
    int screen_h = vinfo.yres;
    int bpp = vinfo.bits_per_pixel;
    int line_bytes = finfo.line_length;
    size_t map_size = finfo.smem_len;

    printf("[lilsmoke-fbtest] screen: %dx%d @ %dbpp\n", screen_w, screen_h, bpp);
    printf("[lilsmoke-fbtest] line_length: %d bytes, smem_len: %zu bytes\n",
           line_bytes, map_size);
    printf("[lilsmoke-fbtest] red:   off=%u len=%u\n", vinfo.red.offset,   vinfo.red.length);
    printf("[lilsmoke-fbtest] green: off=%u len=%u\n", vinfo.green.offset, vinfo.green.length);
    printf("[lilsmoke-fbtest] blue:  off=%u len=%u\n", vinfo.blue.offset,  vinfo.blue.length);
    printf("[lilsmoke-fbtest] alpha: off=%u len=%u\n", vinfo.transp.offset, vinfo.transp.length);

    /* --- 3. mmap the framebuffer --- */
    uint8_t *fb = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) {
        perror("[lilsmoke-fbtest] mmap");
        close(fd);
        return 1;
    }
    printf("[lilsmoke-fbtest] mmap'd %zu bytes at %p\n", map_size, (void *)fb);

    /* --- 4. Clear screen to dark grey --- */
    uint32_t grey_px = make_pixel(0x2B, 0x2D, 0x31, bpp);
    for (int y = 0; y < screen_h; y++) {
        for (int x = 0; x < screen_w; x++) {
            if (bpp == 32) {
                *(uint32_t *)(fb + y * line_bytes + x * 4) = grey_px;
            } else if (bpp == 16) {
                *(uint16_t *)(fb + y * line_bytes + x * 2) = (uint16_t)grey_px;
            }
        }
    }
    printf("[lilsmoke-fbtest] cleared screen\n");

    /* --- 5. Load logo PNG, or draw fallback --- */
    int logo_w, logo_h;
    uint8_t *logo = load_png_rgba(logo_path, &logo_w, &logo_h);
    if (!logo) {
        printf("[lilsmoke-fbtest] logo PNG not found, drawing fallback\n");
        logo_w = LOGO_FALLBACK_W;
        logo_h = LOGO_FALLBACK_H;
        logo = malloc(logo_w * logo_h * 4);
        if (!logo) {
            munmap(fb, map_size);
            close(fd);
            return 1;
        }
        draw_fallback(logo, logo_w, logo_h);
    } else {
        printf("[lilsmoke-fbtest] logo loaded: %dx%d\n", logo_w, logo_h);
    }

    /* --- 6. Blit logo centered, respecting alpha --- */
    int dst_x = (screen_w - logo_w) / 2;
    int dst_y = (screen_h - logo_h) / 2;
    if (dst_x < 0) dst_x = 0;
    if (dst_y < 0) dst_y = 0;

    for (int y = 0; y < logo_h; y++) {
        int sy = dst_y + y;
        if (sy >= screen_h) break;
        for (int x = 0; x < logo_w; x++) {
            int sx = dst_x + x;
            if (sx >= screen_w) break;
            const uint8_t *src = &logo[(y * logo_w + x) * 4];
            uint8_t r = src[0], g = src[1], b = src[2], a = src[3];
            if (a == 0) continue;
            uint8_t *dst;
            if (bpp == 32) {
                dst = fb + sy * line_bytes + sx * 4;
                /* Simple alpha blend against the cleared grey background */
                if (a == 255) {
                    /* BGRA32: store as B,G,R,A */
                    dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 0xFF;
                } else {
                    uint8_t bg_r = 0x2B, bg_g = 0x2D, bg_b = 0x31;
                    uint8_t out_r = (r * a + bg_r * (255 - a)) / 255;
                    uint8_t out_g = (g * a + bg_g * (255 - a)) / 255;
                    uint8_t out_b = (b * a + bg_b * (255 - a)) / 255;
                    dst[0] = out_b; dst[1] = out_g; dst[2] = out_r; dst[3] = 0xFF;
                }
            } else if (bpp == 16) {
                dst = fb + sy * line_bytes + sx * 2;
                uint16_t px = make_pixel(r, g, b, 16);
                *(uint16_t *)dst = px;
            }
        }
    }
    printf("[lilsmoke-fbtest] logo blitted at (%d, %d)\n", dst_x, dst_y);

    /* --- 7. Print footer text via simple bitmap font (skip for now, just draw a bar) --- */
    int bar_y = screen_h - 8;
    for (int x = 0; x < screen_w; x++) {
        if (bpp == 32) {
            *(uint32_t *)(fb + bar_y * line_bytes + x * 4) =
                make_pixel(0xD9, 0xC9, 0xA3, 32);
        }
    }

    /* --- 8. Wait DISPLAY_SECONDS, then exit --- */
    printf("[lilsmoke-fbtest] displaying for %d seconds...\n", DISPLAY_SECONDS);
    for (int i = 0; i < DISPLAY_SECONDS; i++) {
        sleep_ms(1000);
    }

    /* --- 9. Cleanup --- */
    free(logo);
    munmap(fb, map_size);
    close(fd);

    printf("[lilsmoke-fbtest] done.\n");
    return 0;
}
