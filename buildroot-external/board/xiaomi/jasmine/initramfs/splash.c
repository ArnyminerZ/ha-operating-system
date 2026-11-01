// SPDX-License-Identifier: Apache-2.0
//
// Boot splash for the Mi A2: a house logo with a spinner, drawn on the
// framebuffer from the initramfs until the Home Assistant CLI starts (it kills
// this program). The framebuffer of the bootloader is replaced by the one of
// the msm driver while booting, so the device is reopened when it changes.

#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define BG_R 17
#define BG_G 24
#define BG_B 39
#define FG_R 3
#define FG_G 169
#define FG_B 244

#define FRAME_US 50000
#define CHECK_FRAMES 10

struct screen {
	uint8_t *mem;
	size_t len;
	int w, h, stride, bpp;
	int shift_r, shift_g, shift_b;
};

static volatile sig_atomic_t stop;

static void on_term(int sig)
{
	(void)sig;
	stop = 1;
}

static inline void put(struct screen *s, int x, int y, int r, int g, int b)
{
	uint32_t v;

	if (x < 0 || y < 0 || x >= s->w || y >= s->h)
		return;
	v = ((uint32_t)r << s->shift_r) | ((uint32_t)g << s->shift_g) | ((uint32_t)b << s->shift_b);
	*(uint32_t *)(s->mem + (size_t)y * s->stride + (size_t)x * 4) = v;
}

static inline void blend(struct screen *s, int x, int y, float a)
{
	put(s, x, y, BG_R + (FG_R - BG_R) * a, BG_G + (FG_G - BG_G) * a, BG_B + (FG_B - BG_B) * a);
}

// Coverage of the house silhouette at a point, in a unit box (0..1 on both axes)
static int in_house(float x, float y)
{
	// roof: triangle with the peak at the top, walls below it
	if (y >= 0.0f && y <= 0.42f) {
		float half = y * 1.19f;		// 0.5 / 0.42

		if (fabsf(x - 0.5f) <= half)
			return 1;
	}
	if (y > 0.42f && y <= 1.0f && x >= 0.14f && x <= 0.86f) {
		// door
		if (x >= 0.40f && x <= 0.60f && y >= 0.70f)
			return 0;
		return 1;
	}
	return 0;
}

static void draw_logo(struct screen *s)
{
	int size = s->w * 40 / 100;
	int x0 = (s->w - size) / 2;
	int y0 = s->h * 30 / 100;
	int x, y, sx, sy;

	for (y = 0; y < size; y++) {
		for (x = 0; x < size; x++) {
			int hit = 0;

			for (sy = 0; sy < 3; sy++)
				for (sx = 0; sx < 3; sx++)
					hit += in_house((x + (sx + 0.5f) / 3) / size, (y + (sy + 0.5f) / 3) / size);
			blend(s, x0 + x, y0 + y, hit / 9.0f);
		}
	}
}

static void clear(struct screen *s)
{
	int x, y;

	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++)
			put(s, x, y, BG_R, BG_G, BG_B);
}

static void draw_spinner(struct screen *s, float head)
{
	int radius = s->w * 6 / 100;
	float thick = radius * 0.22f;
	int cx = s->w / 2;
	int cy = s->h * 30 / 100 + s->w * 40 / 100 + s->w * 14 / 100;
	int x, y, box = radius + (int)thick + 2;

	for (y = -box; y <= box; y++) {
		for (x = -box; x <= box; x++) {
			float r = hypotf(x, y);
			float ring = thick / 2 + 0.5f - fabsf(r - radius);
			float ang, behind, tail;

			if (ring <= 0.0f) {
				put(s, cx + x, cy + y, BG_R, BG_G, BG_B);
				continue;
			}
			if (ring > 1.0f)
				ring = 1.0f;
			// angle behind the head, 0..2pi
			ang = atan2f(y, x);
			behind = fmodf(head - ang + 4 * (float)M_PI, 2 * (float)M_PI);
			tail = 1.0f - behind / (1.6f * (float)M_PI);
			if (tail < 0.12f)
				tail = 0.12f;
			blend(s, cx + x, cy + y, ring * tail);
		}
	}
}

#ifndef TEST
static int open_screen(struct screen *s, char *name, size_t name_len)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	FILE *f;
	int fd = open("/dev/fb0", O_RDWR);

	if (fd < 0)
		return -1;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0 ||
	    var.bits_per_pixel != 32) {
		close(fd);
		return -1;
	}
	s->len = fix.smem_len;
	s->mem = mmap(NULL, s->len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (s->mem == MAP_FAILED)
		return -1;
	s->w = var.xres;
	s->h = var.yres;
	s->stride = fix.line_length;
	s->bpp = 32;
	s->shift_r = var.red.offset;
	s->shift_g = var.green.offset;
	s->shift_b = var.blue.offset;

	name[0] = 0;
	f = fopen("/sys/class/graphics/fb0/name", "r");
	if (f) {
		if (fgets(name, name_len, f))
			name[strcspn(name, "\n")] = 0;
		fclose(f);
	}
	return 0;
}

static void current_name(char *name, size_t len)
{
	FILE *f = fopen("/sys/class/graphics/fb0/name", "r");

	name[0] = 0;
	if (f) {
		if (fgets(name, len, f))
			name[strcspn(name, "\n")] = 0;
		fclose(f);
	}
}

int main(void)
{
	struct sigaction sa = { .sa_handler = on_term };
	struct screen s;
	char name[64] = "", now[64];
	float head = 0.0f;
	int frame = 0, have = 0;

	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	while (!stop) {
		if (!have) {
			if (open_screen(&s, name, sizeof(name)) < 0) {
				usleep(200000);
				continue;
			}
			clear(&s);
			draw_logo(&s);
			have = 1;
		}

		draw_spinner(&s, head);
		head = fmodf(head + 0.28f, 2 * (float)M_PI);
		usleep(FRAME_US);

		// a new framebuffer device replaced the one we draw on
		if (++frame % CHECK_FRAMES == 0) {
			current_name(now, sizeof(now));
			if (strcmp(now, name)) {
				munmap(s.mem, s.len);
				have = 0;
			}
		}
	}
	return 0;
}
#else
// Render one frame to a PPM file for a look on the PC: splash-test out.ppm [w h]
int main(int argc, char **argv)
{
	struct screen s = { .w = argc > 2 ? atoi(argv[2]) : 1080, .h = argc > 3 ? atoi(argv[3]) : 2160, .bpp = 32,
			    .shift_r = 16, .shift_g = 8, .shift_b = 0 };
	FILE *f = fopen(argv[1], "w");
	int i;

	s.stride = s.w * 4;
	s.mem = malloc((size_t)s.stride * s.h);
	clear(&s);
	draw_logo(&s);
	draw_spinner(&s, 1.0f);
	fprintf(f, "P6\n%d %d\n255\n", s.w, s.h);
	for (i = 0; i < s.w * s.h; i++) {
		uint32_t v = ((uint32_t *)s.mem)[i];

		fputc((v >> 16) & 255, f);
		fputc((v >> 8) & 255, f);
		fputc(v & 255, f);
	}
	fclose(f);
	return 0;
}
#endif
