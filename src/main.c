/**
 * Tympanometer Display - GME12864-13 (128x64 SSD1306)
 * Single screen: stats in yellow zone (rows 0-15), tympanogram in blue zone (rows 16-63)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <string.h>

#define W    128
#define H    64

static const struct device *disp;
static uint8_t fb[W * H / 8];  /* vertical-page layout: fb[(y>>3)*W+x], bit (y&7) */

static struct {
	char type[3];      /* "A", "As", "Ad", "B", "C" */
	int16_t ecv_x10;   /* 0.1 mL units  (11 = 1.1 mL) */
	int16_t ppp;       /* daPa */
	int16_t sa_x100;   /* 0.01 mL units (65 = 0.65 mL) */
} tymp;

struct tymp_point {
	int16_t pressure_dapa;   /* -400 to +200 daPa */
	int16_t admittance_x100; /* 0.01 mL units, e.g. 110 = 1.10 mL */
};

/* 3x5 bitmap font, column-major, bit0 = top row */
static const uint8_t font[][3] = {
	{0x1F, 0x11, 0x1F}, /* 0  (0) */
	{0x12, 0x1F, 0x10}, /* 1  (1) */
	{0x1D, 0x15, 0x17}, /* 2  (2) */
	{0x15, 0x15, 0x1F}, /* 3  (3) */
	{0x07, 0x04, 0x1F}, /* 4  (4) */
	{0x17, 0x15, 0x1D}, /* 5  (5) */
	{0x1F, 0x15, 0x1D}, /* 6  (6) */
	{0x01, 0x01, 0x1F}, /* 7  (7) */
	{0x1F, 0x15, 0x1F}, /* 8  (8) */
	{0x17, 0x15, 0x1F}, /* 9  (9) */
	{0x04, 0x0E, 0x04}, /* +  (10) */
	{0x04, 0x04, 0x04}, /* -  (11) */
	{0x10, 0x00, 0x00}, /* .  (12) */
	{0x1E, 0x05, 0x1E}, /* A  (13) */
	{0x1F, 0x15, 0x0A}, /* B  (14) */
	{0x0E, 0x11, 0x11}, /* C  (15) */
	{0x1F, 0x15, 0x11}, /* E  (16) */
	{0x1F, 0x10, 0x10}, /* L  (17) */
	{0x1F, 0x05, 0x02}, /* P  (18) */
	{0x12, 0x15, 0x09}, /* S  (19) */
	{0x0F, 0x10, 0x0F}, /* V  (20) */
	{0x0C, 0x16, 0x1E}, /* a  (21) */
	{0x1C, 0x14, 0x1F}, /* d  (22) */
	{0x1E, 0x06, 0x1E}, /* m  (23) */
};

static int fidx(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	switch (c) {
	case '+': return 10;
	case '-': return 11;
	case '.': return 12;
	case 'A': return 13;
	case 'B': return 14;
	case 'C': return 15;
	case 'E': return 16;
	case 'L': return 17;
	case 'P': return 18;
	case 'S': return 19;
	case 's': return 19;
	case 'V': return 20;
	case 'a': return 21;
	case 'd': return 22;
	case 'm': return 23;
	}
	return -1;
}

static inline void setpx(int x, int y)
{
	if ((unsigned)x < W && (unsigned)y < H)
		fb[(y >> 3) * W + x] |= 1 << (y & 7);
}

/* Horizontal and vertical line drawing */
static void hline(int x0, int x1, int y)  { for (int x = x0; x <= x1; x++) setpx(x, y); }
static void vline(int x, int y0, int y1)  { for (int y = y0; y <= y1; y++) setpx(x, y); }

/* Render a single scaled glyph at (x, y) with pixel scale sx*sy */
static void glyph(int x, int y, char c, int sx, int sy)
{
	int i = fidx(c);
	if (i < 0) return;
	for (int col = 0; col < 3; col++) {
		uint8_t b = font[i][col];
		for (int row = 0; row < 5; row++)
			if (b & (1 << row))
				for (int dy = 0; dy < sy; dy++)
					for (int dx = 0; dx < sx; dx++)
						setpx(x + col * sx + dx, y + row * sy + dy);
	}
}

/* Render a string; '.' advances 2 columns, all others 4 columns (scaled) */
static int text(int x, int y, const char *str, int sx, int sy)
{
	for (; *str; str++) {
		if (*str != ' ')
			glyph(x, y, *str, sx, sy);
		x += (*str == '.') ? 2 * sx : 4 * sx;
	}
	return x;
}

/* Integer and fixed-point number rendering helpers */
static int uint_to_str(char *buf, unsigned v)
{
	char tmp[6]; int n = 0;
	do { tmp[n++] = '0' + v % 10; v /= 10; } while (v);
	int i = 0;
	while (n--) buf[i++] = tmp[n];
	return i;
}

static int num(int x, int y, int v, int sx, int sy)
{
	char buf[8]; int i = 0;
	if (v < 0) { buf[i++] = '-'; v = -v; }
	i += uint_to_str(buf + i, v);
	buf[i] = 0;
	return text(x, y, buf, sx, sy);
}

/* decN: render fixed-point with `dec` decimal places, e.g. decN(..., 11, 1, ...) -> "1.1" */
static int decN(int x, int y, int v, int dec, int sx, int sy)
{
	char buf[12]; int i = 0;
	if (v < 0) { buf[i++] = '-'; v = -v; }
	int divisor = 1;
	for (int d = 0; d < dec; d++) divisor *= 10;
	i += uint_to_str(buf + i, v / divisor);
	buf[i++] = '.';
	int frac = v % divisor;
	for (int d = dec - 1; d >= 0; d--) {
		int p = 1; for (int k = 0; k < d; k++) p *= 10;
		buf[i++] = '0' + (frac / p) % 10;
	}
	buf[i] = 0;
	return text(x, y, buf, sx, sy);
}

/*
 * Render tympanogram to OLED.
 *   type      - curve type string: "A", "As", "Ad", "B", or "C"
 *   ecv_x10   - ear canal volume in 0.1 mL units  (e.g. 11 = 1.1 mL)
 *   ppp       - peak pressure in daPa              (e.g. -15)
 *   sa_x100   - static admittance in 0.01 mL units (e.g. 65 = 0.65 mL)
 *   pts / n   - array of data points to plot
 */
static void show(const char *type, int16_t ecv_x10,
		 int16_t ppp, int16_t sa_x100,
		 const struct tymp_point *pts, int n)
{
	strcpy(tymp.type, type);
	tymp.ecv_x10 = ecv_x10;
	tymp.ppp = ppp;
	tymp.sa_x100 = sa_x100;

	memset(fb, 0, sizeof(fb));

	/* --- Yellow zone (rows 0-15): stats --- */
	/* Type: first char (A) at full height (sy=3, 15px); suffix (s/d) smaller (sy=2, 10px), bottom-aligned */
	glyph(1, 1, tymp.type[0], 2, 3);
	if (tymp.type[1])
		glyph(9, 6, tymp.type[1], 2, 2);
	text(19, 0, "ECV mL", 1, 1);
	text(60, 0, "SA mL", 1, 1);
	text(97, 0, "PPP daPa", 1, 1);
	decN(19, 6, tymp.ecv_x10, 1, 2, 2);
	decN(56, 6, tymp.sa_x100, 2, 2, 2);
	num(97, 6, tymp.ppp, 2, 2);

	/* --- Blue zone (rows 16-63): tympanogram --- */
	int gy = 16, gh = 47;
	int bot = gy + gh - 1;

#define Y_MAX_X100 180  /* y-axis max: 1.80 mL */

	/* X-axis ticks: -400 to +200 daPa every 100; 0 daPa tick is 5px tall */
	for (int p = -400; p <= 200; p += 100) {
		int tx = 1 + (p + 400) * (W - 2) / 600;
		vline(tx, bot - (p == 0 ? 5 : 1), bot);
	}

	/* Y-axis ticks: 0.0 to 1.8 mL every 0.2; 1.0 mL tick is 5px wide */
	for (int a = 0; a <= Y_MAX_X100; a += 20) {
		int ty = bot - 1 - a * (gh - 3) / Y_MAX_X100;
		hline(0, (a == 100 ? 5 : 1), ty);
	}

	/* Plot curve with linear interpolation between points */
	int px = -1, py = -1;
	for (int i = 0; i < n; i++) {
		int cx = 1 + (pts[i].pressure_dapa + 400) * (W - 2) / 600;
		int cy = bot - 1 - pts[i].admittance_x100 * (gh - 3) / Y_MAX_X100;
		if (cy < gy) cy = gy;
		setpx(cx, cy);
		if (px >= 0) {
			int dx = cx - px, dy = cy - py;
			int steps = (dx > 0 ? dx : -dx);
			int ay = (dy > 0 ? dy : -dy);
			if (ay > steps) steps = ay;
			for (int s = 1; s < steps; s++)
				setpx(px + dx * s / steps, py + dy * s / steps);
		}
		px = cx; py = cy;
	}

	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(fb), .width = W, .height = H, .pitch = W,
	};
	display_write(disp, 0, 0, &desc, fb);
}

/*
 * Generate a Lorentzian demo sweep peaked at ppp_dapa.
 * Replace with real ADC data when available.
 *   sa_x100   - peak admittance in 0.01 mL units
 *   base_x100 - baseline admittance (floor)
 *   width     - sharpness: smaller = narrower peak (try 25–150; use 10000 for flat)
 */
static void make_demo(struct tymp_point *pts, int n, int16_t ppp_dapa,
		      int sa_x100, int base_x100, int width)
{
	int peak = (400 + ppp_dapa) * (n - 1) / 600;

	for (int i = 0; i < n; i++) {
		pts[i].pressure_dapa = (int16_t)(-400 + i * 600 / (n - 1));
		int d = i - peak;
		int k = (d > 0) ? 3 : 2;  /* slightly steeper on positive-pressure side */
		int v = sa_x100 * width / (width + d * d * k);
		pts[i].admittance_x100 = (int16_t)(v < base_x100 ? base_x100 : v);
	}
}

int main(void)
{
	disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(disp))
		return -1;

	display_blanking_off(disp);

	static struct tymp_point pts[64];

	// Example show() calls — uncomment one to test.
	// Args: type, ecv_x10 (0.1 mL), ppp (daPa), sa_x100 (0.01 mL), pts, n

	// Type A  — normal: medium sharp peak at 0 daPa, SA=0.65 mL
	make_demo(pts, 64,    0,  65, 10,  50);
	show("A",  14,   0,  65, pts, 64);
	k_sleep(K_SECONDS(1));
	// Type As — stiff: broad shallow peak at 0 daPa, SA=0.25 mL
	make_demo(pts, 64,    0,  25, 10, 150);
	show("As", 12,   0,  25, pts, 64);
	k_sleep(K_SECONDS(1));
	// Type Ad — flaccid: narrow very tall peak at 0 daPa, SA=1.60 mL
	make_demo(pts, 64,    0, 160, 10,  25);
	show("Ad", 13,   0, 160, pts, 64);
	k_sleep(K_SECONDS(1));
	// Type B  — flat: no peak (fluid), SA=0.10 mL
	make_demo(pts, 64,    0,  10, 10, 10000);
	show("B",   8,   0,  10, pts, 64);
	k_sleep(K_SECONDS(1));
	// Type C  — negative pressure: peak at -150 daPa, SA=0.55 mL
	make_demo(pts, 64, -150,  55, 10,  50);
	show("C",  11, -150,  55, pts, 64);

	while (1)
		k_sleep(K_FOREVER);
}
