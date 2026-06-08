#include "display.h" // includes my own header, so that .c knows about tymp_results, display_state, and the macros
#include <zephyr/kernel.h> // timers, sleep
#include <zephyr/device.h> // device binding macros
#include <zephyr/drivers/display.h> // display driver API
#include <string.h> // memset, strcpy
#include <stdint.h>
#include <stdbool.h>

static const struct device *disp; // global variable for the display device
static uint8_t fb[DISPLAY_W * DISPLAY_H / 8]; // framebuffer for the display, 1 bit per pixel

static struct tymp_results cur; // global variable to hold the current results, so that display can access them. I didn't use a pointer, to make sure the memory doesn't have to stay valid forever. 
static int results_page; // global variable to track which page of results we're on, since there are more results than can fit on the display at once

static struct tymp_results res_left, res_right; // global variables to hold the left and right ear results, so that we can display them when we have them both. I didn't use pointers, to make sure the memory doesn't have to stay valid forever.
static bool have_left, have_right; // global variables to track whether we have the left and right ear results, so that we know when to display them.

static int tilt_x_deg, tilt_y_deg; // global variables to hold the current tilt of the device, which will affect the display during seeking seal
static int isqrt(int v) { int r = 0; while ((r+1)*(r+1) <= v) r++; return r; }
static int batt_pct = 100; // global variable for battery percentage, initialized to 100%
static char cur_ear = 'L'; // global variable for current ear, initialized to 'L'
static bool post_ok[5]; // global variable for POST results
static int seal_phase; // global variable for seal phase, which will affect the display during seeking seal

static const uint8_t font[][3] = {
	{0x1F,0x11,0x1F},{0x12,0x1F,0x10},{0x1D,0x15,0x17},{0x15,0x15,0x1F}, /* 0-3 */
	{0x07,0x04,0x1F},{0x17,0x15,0x1D},{0x1F,0x15,0x1D},{0x01,0x01,0x1F}, /* 4-7 */
	{0x1F,0x15,0x1F},{0x17,0x15,0x1F},                                   /* 8-9 */
	{0x04,0x0E,0x04},{0x04,0x04,0x04},{0x10,0x00,0x00},                  /* + - . */
	{0x1E,0x05,0x1E},{0x1F,0x15,0x0A},{0x0E,0x11,0x11},{0x1F,0x15,0x11}, /* A B C E */
	{0x1F,0x10,0x10},{0x1F,0x05,0x02},{0x12,0x15,0x09},                  /* L P S */
	{0x07,0x18,0x07},                                                    /* V (improved) */
	{0x0C,0x16,0x1E},{0x1C,0x14,0x1F},{0x1E,0x06,0x1E},                  /* a d m */
	{0x1F,0x05,0x1A},{0x01,0x1F,0x01},                                   /* R T (new) */
};
static int fidx(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	switch (c) {
	case '+': return 10; case '-': return 11; case '.': return 12;
	case 'A': return 13; case 'B': return 14; case 'C': return 15;
	case 'E': return 16; case 'L': return 17; case 'P': return 18;
	case 'S': case 's': return 19; case 'V': return 20;
	case 'a': return 21; case 'd': return 22; case 'm': return 23;
	case 'R': return 24; case 'T': return 25;
	}
	return -1;
}

static inline void setpx(int x, int y)
{
	if ((unsigned)x < DISPLAY_W && (unsigned)y < DISPLAY_H)
		fb[(y >> 3) * DISPLAY_W + x] |= 1 << (y & 7);
}
static void hline(int x0, int x1, int y) { for (int x = x0; x <= x1; x++) setpx(x, y); }
static void vline(int x, int y0, int y1) { for (int y = y0; y <= y1; y++) setpx(x, y); }

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
						setpx(x + col*sx + dx, y + row*sy + dy);
	}
}
static int text(int x, int y, const char *s, int sx, int sy)
{
	for (; *s; s++) {
		if (*s != ' ') glyph(x, y, *s, sx, sy);
		x += (*s == '.') ? 2*sx : 4*sx;
	}
	return x;
}
static int uint_to_str(char *buf, unsigned v)
{
	char tmp[6]; int n = 0;
	do { tmp[n++] = '0' + v % 10; v /= 10; } while (v);
	int i = 0; while (n--) buf[i++] = tmp[n];
	return i;
}
static int num(int x, int y, int v, int sx, int sy)
{
	char buf[8]; int i = 0;
	if (v < 0) { buf[i++] = '-'; v = -v; }
	i += uint_to_str(buf + i, v); buf[i] = 0;
	return text(x, y, buf, sx, sy);
}
static int decN(int x, int y, int v, int dec, int sx, int sy)
{
	char buf[12]; int i = 0;
	if (v < 0) { buf[i++] = '-'; v = -v; }
	int divisor = 1; for (int d = 0; d < dec; d++) divisor *= 10;
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

static const struct { char c; uint8_t col[5]; } font57[] = {
	{'A',{0x7E,0x09,0x09,0x09,0x7E}},{'B',{0x7F,0x49,0x49,0x49,0x36}},
	{'C',{0x3E,0x41,0x41,0x41,0x22}},{'D',{0x7F,0x41,0x41,0x22,0x1C}},
	{'E',{0x7F,0x49,0x49,0x49,0x41}},{'F',{0x7F,0x09,0x09,0x09,0x01}},
	{'G',{0x3E,0x41,0x49,0x49,0x3A}},{'H',{0x7F,0x08,0x08,0x08,0x7F}},
	{'I',{0x00,0x41,0x7F,0x41,0x00}},{'J',{0x30,0x40,0x41,0x3F,0x01}},
	{'K',{0x7F,0x08,0x1C,0x22,0x41}},{'L',{0x7F,0x40,0x40,0x40,0x40}},
	{'M',{0x7F,0x02,0x0C,0x02,0x7F}},{'N',{0x7F,0x02,0x0C,0x10,0x7F}},
	{'O',{0x3E,0x41,0x41,0x41,0x3E}},{'P',{0x7F,0x09,0x09,0x09,0x06}},
	{'R',{0x7F,0x09,0x19,0x29,0x46}},{'S',{0x46,0x49,0x49,0x09,0x30}},
	{'T',{0x01,0x01,0x7F,0x01,0x01}},{'U',{0x3F,0x40,0x40,0x40,0x3F}},
	{'V',{0x0F,0x30,0x40,0x30,0x0F}},{'W',{0x7F,0x20,0x18,0x20,0x7F}},
	{'X',{0x63,0x14,0x08,0x14,0x63}},{'Y',{0x03,0x04,0x78,0x04,0x03}},
	{'Z',{0x61,0x51,0x49,0x45,0x43}},
	{'0',{0x3E,0x51,0x4D,0x43,0x3E}},{'1',{0x00,0x42,0x7F,0x40,0x00}},
	{'2',{0x62,0x51,0x49,0x49,0x46}},{'3',{0x21,0x41,0x45,0x47,0x39}},
	{'4',{0x18,0x14,0x12,0x7F,0x10}},{'5',{0x27,0x45,0x45,0x45,0x39}},
	{'6',{0x3C,0x4A,0x49,0x49,0x30}},{'7',{0x01,0x71,0x09,0x05,0x03}},
	{'8',{0x36,0x49,0x49,0x49,0x36}},{'9',{0x06,0x49,0x49,0x29,0x1E}},
	{' ',{0x00,0x00,0x00,0x00,0x00}},{'.',{0x00,0x60,0x60,0x00,0x00}},
	{'-',{0x00,0x08,0x08,0x08,0x00}},{'!',{0x00,0x00,0x5F,0x00,0x00}},
	{':',{0x00,0x36,0x36,0x00,0x00}},
};
static const uint8_t *font57_lookup(char c)
{
	for (unsigned i = 0; i < sizeof(font57)/sizeof(font57[0]); i++)
		if (font57[i].c == c) return font57[i].col;
	return NULL;
}
static void glyph57(int x, int y, char c, int sx, int sy)
{
	const uint8_t *g = font57_lookup(c);
	if (!g) return;
	for (int col = 0; col < 5; col++) {
		uint8_t b = g[col];
		for (int row = 0; row < 7; row++)
			if (b & (1 << row))
				for (int dy = 0; dy < sy; dy++)
					for (int dx = 0; dx < sx; dx++)
						setpx(x + col*sx + dx, y + row*sy + dy);
	}
}
static int text57(int x, int y, const char *s, int sx)
{
	for (; *s; s++) { glyph57(x, y, *s, sx, sx); x += 6 * sx; }
	return x;
}
static void ctext57(int y, const char *s, int sx)
{
	int w = 0; for (const char *p = s; *p; p++) w += 6 * sx;
	text57((DISPLAY_W - w) / 2, y, s, sx);
}

static void circle(int cx, int cy, int r) {
	int x = r, y = 0, err = 1 - r;
	while (x >= y) {
		setpx(cx+x,cy+y); setpx(cx+y,cy+x); setpx(cx-x,cy+y); setpx(cx-y,cy+x);
		setpx(cx-x,cy-y); setpx(cx-y,cy-x); setpx(cx+x,cy-y); setpx(cx+y,cy-x);
		y++;
		if (err < 0) err += 2*y + 1;
		else { x--; err += 2*(y - x) + 1; }
	}
}

static void draw_battery(int x, int y, int pct)
{
	hline(x, x+10, y); hline(x, x+10, y+6);
	vline(x, y, y+6);  vline(x+10, y, y+6);
	vline(x+11, y+2, y+4);
	int bars = (pct * 3 + 50) / 100;
	for (int b = 0; b < bars; b++)
		for (int xx = x+2+b*3; xx < x+4+b*3; xx++) vline(xx, y+2, y+4);
}

static void disc(int cx, int cy, int r)          /* filled disc */
{
	for (int yy = -r; yy <= r; yy++)
		for (int xx = -r; xx <= r; xx++)
			if (xx*xx + yy*yy <= r*r) setpx(cx+xx, cy+yy);
}

static const char *ble_rows[9] = {
	"..#..","..##.","#.#.#",".###.","..#..",".###.","#.#.#","..##.","..#.."
};
static void draw_ble(int x, int y)
{
	for (int r = 0; r < 9; r++)
		for (int c = 0; c < 5; c++)
			if (ble_rows[r][c] == '#') setpx(x + c, y + r);
}
static void draw_check(int x, int y)
{
	setpx(x+4,y); setpx(x+3,y+1); setpx(x,y+2); setpx(x+2,y+2);
	setpx(x+1,y+3); setpx(x+2,y+4);
}
static void draw_warn(int cx, int y)            /* warning triangle with ! */
{
	for (int i = 0; i < 11; i++) { setpx(cx-i, y+i); setpx(cx+i, y+i); }
	hline(cx-10, cx+10, y+11);
	vline(cx, y+3, y+7); setpx(cx, y+9);
}
static void draw_x(int x, int y)                /* 13x13 error X */
{
	for (int i = 0; i < 13; i++) { setpx(x+i, y+i); setpx(x+12-i, y+i); }
}
static void draw_dot(int x, int y, bool filled) /* 3x3 filled or outline */
{
	for (int yy = y; yy < y+3; yy++)
		for (int xx = x; xx < x+3; xx++)
			if (filled || xx==x || xx==x+2 || yy==y || yy==y+2) setpx(xx, yy);
}

int display_init(void) {
    disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display)); // get the display device from the device tree
    if (!device_is_ready(disp))
        return -ENODEV; // no such device error if display is not ready
    display_blanking_off(disp); // turn on the display
    return 0;   
    }

void display_blank(void) {
    display_blanking_on(disp); // turn off the display. this is what measuring and sleep modes will call to save power
}
 
static void flush(void)
{
	display_blanking_off(disp);          /* panel on whenever we draw */
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(fb),
		.width    = DISPLAY_W,
		.height   = DISPLAY_H,
		.pitch    = DISPLAY_W,
	};
	display_write(disp, 0, 0, &desc, fb);
}

static void draw_pinned(void) {
    glyph(STAT_TYPE_X, STAT_TYPE_Y, cur.type[0], 2, 3);
    if (cur.type[1])
        glyph(STAT_TYPE_SUFFIX_X, STAT_TYPE_SUFFIX_Y, cur.type[1], 2, 3);
    glyph(STAT_EAR_X, STAT_EAR_Y, cur.ear, 2, 2);       
    }

static void draw_page(void) {
    if (results_page == 0) {
        decN(CONF_X, STAT_VALUE_Y, cur.confidence_x100, 2, 2, 2);
    } else {
        text(ECV_X, STAT_LABEL_Y, "ECV", 1, 1);
        decN(ECV_X, STAT_VALUE_Y, cur.ecv_x10, 1, 2, 2);
        text(SA_X, STAT_LABEL_Y, "SA", 1, 1);
        decN(SA_X, STAT_VALUE_Y, cur.sa_x100, 2, 2, 2);
        text(TPP_X, STAT_LABEL_Y, "TPP", 1, 1);
        num(TPP_X, STAT_VALUE_Y, cur.tpp_dapa, 2, 2);
        }
}

static void draw_curve(void) //James Curve Plotter
{
	int span = GRAPH_P_MAX_DAPA - GRAPH_P_MIN_DAPA;          /* 600 daPa */
	int bot  = GRAPH_Y + GRAPH_H - 1;                        /* bottom row, 62 */
	int x0   = 1 + (0 - GRAPH_P_MIN_DAPA) * (DISPLAY_W - 2) / span;        /* 0 daPa column */
	int y10  = bot - 1 - 100 * (GRAPH_H - 3) / GRAPH_Y_MAX_X100;          /* 1.0 mL row */

	/* dotted gridlines: baseline, 1.0 mL, and the 0 daPa vertical */
	for (int x = 0; x < DISPLAY_W; x += 2) { setpx(x, bot); setpx(x, y10); }
	for (int y = GRAPH_Y + 1; y < bot; y += 2) setpx(x0, y);

	/* the curve itself, with linear interpolation between points */
	int px = -1, py = -1;
	for (int i = 0; i < cur.n_points; i++) {
		int cx = 1 + (cur.points[i].pressure_dapa - GRAPH_P_MIN_DAPA) * (DISPLAY_W - 2) / span;
		int cy = bot - 1 - cur.points[i].admittance_x100 * (GRAPH_H - 3) / GRAPH_Y_MAX_X100;
		if (cy < GRAPH_Y) cy = GRAPH_Y;                 /* clamp to top of plot */
		setpx(cx, cy);
		if (px >= 0) {
			int dx = cx - px, dy = cy - py;
			int steps = (dx < 0 ? -dx : dx);
			int ady   = (dy < 0 ? -dy : dy);
			if (ady > steps) steps = ady;
			for (int s = 1; s < steps; s++)
				setpx(px + dx * s / steps, py + dy * s / steps);
		}
		px = cx; py = cy;
	}
}

static void draw_page_dots(void) {
    draw_dot(DOT1_X, DOT_Y, results_page == 0);
    draw_dot(DOT2_X, DOT_Y, results_page == 1);
}

static void render_results(void) {
    memset(fb, 0, sizeof(fb));
    draw_pinned(); // draw the pinned values on the current page, so that they update when we switch pages
    draw_page(); // draw the page number on the current page, so that it updates when we switch pages
    draw_curve(); // draw the curve on the current page, so that it updates when we switch pages
    draw_page_dots(); // draw the dots on the current page, so that they update when we switch pages
    flush(); // update the display with the new framebuffer
}


static void render_ready(void)
{
	memset(fb, 0, sizeof(fb));
	draw_battery(2, 4, batt_pct);          /* status bar: battery */
	draw_ble(22, 3);                       /* status bar: BLE     */
	glyph(118, 4, cur_ear, 1, 1);          /* status bar: L/R     */
	ctext57(24, "INSERT EAR TIP", 1);
	ctext57(38, "PRESS TO START", 1);
	flush();
}

static void render_boot(void)
{
	static const char *items[] = { "DISP", "MIC", "PRES", "MOTOR", "BLE" };
	memset(fb, 0, sizeof(fb));
	ctext57(2, "SELF TEST", 1);
	for (int i = 0; i < 5; i++) {
		int yy = 14 + i*10;
		text57(6, yy, items[i], 1);
		if (post_ok[i]) draw_check(70, yy+1);   /* tick once that interface passes */
	}
	flush();
}

static void render_error(void)
{
	memset(fb, 0, sizeof(fb));
	draw_x(57, 4);
	ctext57(24, "SENSOR ERROR", 1);
	ctext57(44, "PRESS TO RESTART", 1);
	flush();
}

static void render_measuring(void) { display_blank(); }
static void render_sleep(void)     { display_blank(); }

/* generalized curve: draws into any box (used by Summary's mini-plots) */
static void draw_curve_into(int ox, int oy, int w, int h, const struct tymp_results *r)
{
	int span = GRAPH_P_MAX_DAPA - GRAPH_P_MIN_DAPA;
	int bot  = oy + h - 1;
	int x0   = ox + (0 - GRAPH_P_MIN_DAPA) * (w - 2) / span;
	int y10  = bot - 1 - 100 * (h - 3) / GRAPH_Y_MAX_X100;
	for (int x = ox; x < ox + w; x += 2) { setpx(x, bot); setpx(x, y10); }
	for (int y = oy + 1; y < bot; y += 2) setpx(x0, y);
	int px = -1, py = -1;
	for (int i = 0; i < r->n_points; i++) {
		int cx = ox + (r->points[i].pressure_dapa - GRAPH_P_MIN_DAPA) * (w - 2) / span;
		int cy = bot - 1 - r->points[i].admittance_x100 * (h - 3) / GRAPH_Y_MAX_X100;
		if (cy < oy) cy = oy;
		setpx(cx, cy);
		if (px >= 0) {
			int dx = cx - px, dy = cy - py;
			int steps = (dx < 0 ? -dx : dx), ady = (dy < 0 ? -dy : dy);
			if (ady > steps) steps = ady;
			for (int s = 1; s < steps; s++) setpx(px + dx*s/steps, py + dy*s/steps);
		}
		px = cx; py = cy;
	}
}

static void render_seeking_seal(void)
{
	memset(fb, 0, sizeof(fb));
	ctext57(18, "ADJUST FOR", 1);
	ctext57(30, "SEAL", 1);
	for (int i = 0; i < 3; i++) draw_dot(56 + i*8, 46, i == seal_phase);
	flush();
}

static void render_rearm(void)
{
	memset(fb, 0, sizeof(fb));
	ctext57(16, (cur_ear == 'L') ? "RIGHT EAR NEXT" : "LEFT EAR NEXT", 1);
	ctext57(40, "PRESS TO START", 1);
	flush();
}

static void render_summary(void)
{
	memset(fb, 0, sizeof(fb));
	ctext57(1, "SUMMARY", 1);
	vline(63, 14, 62);
	if (have_left) {
		glyph57(6, 16, 'L', 1, 1);
		glyph57(20, 16, res_left.type[0], 1, 1);
		draw_curve_into(2, 26, 58, 36, &res_left);
	}
	if (have_right) {
		glyph57(70, 16, 'R', 1, 1);
		glyph57(84, 16, res_right.type[0], 1, 1);
		draw_curve_into(66, 26, 58, 36, &res_right);
	}
	flush();
}

static void render_warning(void)
{
	memset(fb, 0, sizeof(fb));
	draw_warn(20, 6);
	text57(40, 10, "LOW", 1);
	text57(40, 24, "BATTERY", 1);
	flush();
}

void display_set_battery(int pct)         { batt_pct = pct; }
void display_set_ear(char lr)             { cur_ear = lr; }
void display_set_post(int index, bool ok) { if (index >= 0 && index < 5) post_ok[index] = ok; }
void display_set_angle(int x_deg, int y_deg) { tilt_x_deg = x_deg; tilt_y_deg = y_deg; }

void display_advance_page(void)
{
	results_page ^= 1;     /* toggle 0 <-> 1 */
	render_results();      /* redraw with the new page */
}

static void page_work_handler(struct k_work *work)
{
	display_advance_page();          /* SPI happens here, in thread context - safe */
}
static K_WORK_DEFINE(page_work, page_work_handler);

static void page_timer_expiry(struct k_timer *t)
{
	k_work_submit(&page_work);        /* interrupt context: just hand off the job */
}

static void render_level(void)
{
	memset(fb, 0, sizeof(fb));

	int mag = isqrt(tilt_x_deg*tilt_x_deg + tilt_y_deg*tilt_y_deg);
	char buf[8]; int i = 0; i += uint_to_str(buf + i, mag); buf[i] = 0;
	int rx = text57(2, 1, buf, 1);
	circle(rx + 1, 2, 1);                          /* tiny degree symbol */

	for (int x = LEVEL_CX-LEVEL_R; x <= LEVEL_CX+LEVEL_R; x += 2) setpx(x, LEVEL_CY);
	for (int y = LEVEL_CY-LEVEL_R; y <= LEVEL_CY+LEVEL_R; y += 2) setpx(LEVEL_CX, y);
	circle(LEVEL_CX, LEVEL_CY, LEVEL_R);
	circle(LEVEL_CX, LEVEL_CY, LEVEL_TOL);

	int lim = LEVEL_R - 2;
	int ox = tilt_x_deg;  if (ox >  lim) ox =  lim; if (ox < -lim) ox = -lim;
	int oy = -tilt_y_deg; if (oy >  lim) oy =  lim; if (oy < -lim) oy = -lim;

	if (mag <= LEVEL_FLAT_DEG) disc(LEVEL_CX, LEVEL_CY, LEVEL_TOL-1);  /* level: fill */
	else                       disc(LEVEL_CX+ox, LEVEL_CY+oy, 4);      /* bubble       */

	flush();
}

static K_TIMER_DEFINE(page_timer, page_timer_expiry, NULL);

void display_show_state(enum display_state state)
{
	if (state != DISPLAY_RESULTS) k_timer_stop(&page_timer);
	switch (state) {
	case DISPLAY_BOOT:         render_boot();         break;
	case DISPLAY_READY:        render_ready();        break;
	case DISPLAY_SEEKING_SEAL: render_seeking_seal(); break;
	case DISPLAY_MEASURING:    render_measuring();    break;   /* blanks */
	case DISPLAY_RESULTS:      render_results();      break;   /* uses cur from display_show_tymp */
	case DISPLAY_REARM:        render_rearm();        break;
	case DISPLAY_SUMMARY:      render_summary();      break;
	case DISPLAY_WARNING:      render_warning();      break;
	case DISPLAY_ERROR:        render_error();        break;
	case DISPLAY_SLEEP:        render_sleep();        break;   /* blanks */
	case DISPLAY_LEVEL:        render_level();        break;
	}
}

void display_show_tymp(const struct tymp_results *res)
{
	cur = *res;
	if (cur.ear == 'R') { res_right = cur; have_right = true; }
	else                { res_left  = cur; have_left  = true; }
	results_page = 0;
	render_results();
	k_timer_start(&page_timer, K_MSEC(RESULTS_PAGE_MS), K_MSEC(RESULTS_PAGE_MS));
}