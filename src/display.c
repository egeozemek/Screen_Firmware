#include "display.h" // includes my own header, so that .c knows about tymp_results, display_state, and the macros
#include <zephyr/kernel.h> // timers, sleep
#include <zephyr/device.h> // device binding macros
#include <zephyr/drivers/display.h> // display driver API
#include <string.h> // memset, strcpy

static const struct device *disp; // global variable for the display device
static uint8_t fb[DISPLAY_W * DISPLAY_H / 8]; // framebuffer for the display, 1 bit per pixel

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
 
static void flush(void) {
    struct display_buffer_descriptor desc = { // describe the framebuffer for the display driver
        .buf_size = sizeof(fb),
        .width = DISPLAY_W,
        .height = DISPLAY_H,
        .pitch = DISPLAY_W / 8, // number of bytes per row
    };
    display_write(disp, 0, 0, &desc, fb); // write the framebuffer to the display 
}

static struct tymp_results cur; // global variable to hold the current results, so that display can access them. I didn't use a pointer, to make sure the memory doesn't have to stay valid forever. 
static int results_page; // global variable to track which page of results we're on, since there are more results than can fit on the display at once

static void render_results(void) {
    memset(fb, 0, sizeof(fb));
    draw_pinned(); // draw the pinned values on the current page, so that they update when we switch pages
    draw_page(); // draw the page number on the current page, so that it updates when we switch pages
    draw_curve(); // draw the curve on the current page, so that it updates when we switch pages
    draw_page_dots(); // draw the dots on the current page, so that they update when we switch pages
    flush(); // update the display with the new framebuffer
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
        decN(TPP_X, STAT_VALUE_Y, cur.tpp_dapa, 2, 2);
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

void display_show_tymp(const struct tymp_results *res) {  
    cur = *res;
    results_page = 0;
    render_results();

}

static void glyph57(int x, int y, char c, int sx, int sy) {
    const uint8_t *g = font57_lookup(c);
    if (!g) return;
    for (int col = 0; col <5; col++) {
        uint8_t b = g[col];
        for (int row = 0; row < 7; row++)
            if (b & (1 << row))
                for (int dy = 0; dy < sy; dy++)
                    for (int dx = 0; dx < sx; dx++)
                        setpx(x + col * sx + dx, y + row * sy + dy);
        }
}

static int text57(int x, int y, const char *s, int sx)
{
	for (; *s; s++) { glyph57(x, y, *s, sx, sx); x += 6 * sx; }   /* 5 wide + 1 gap */
	return x;
}

static void ctext57(int y, const char *s, int sx)   /* horizontally centered */
{
	int w = 0; for (const char *p = s; *p; p++) w += 6 * sx;
	text57((DISPLAY_W - w) / 2, y, s, sx);
}

static void draw_battery(int x, int y, int pct)
{
	hline(x, x+10, y); hline(x, x+10, y+6);      /* top & bottom */
	vline(x, y, y+6);  vline(x+10, y, y+6);      /* sides        */
	vline(x+11, y+2, y+4);                        /* the + nub    */
	int bars = (pct * 3 + 50) / 100;             /* 0..3 fill bars */
	for (int b = 0; b < bars; b++)
		for (int xx = x+2+b*3; xx < x+4+b*3; xx++) vline(xx, y+2, y+4);
}

static void draw_check(int x, int y)   /* a little ✓ */
{
	setpx(x+4,y); setpx(x+3,y+1); setpx(x,y+2); setpx(x+2,y+2);
	setpx(x+1,y+3); setpx(x+2,y+4);
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

void display_show_state(enum display_state state)
{
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
	}
}

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
static K_TIMER_DEFINE(page_timer, page_timer_expiry, NULL);

void display_show_tymp(const struct tymp_results *res)
{
	cur = *res;
	results_page = 0;
	render_results();
	k_timer_start(&page_timer, K_MSEC(RESULTS_PAGE_MS), K_MSEC(RESULTS_PAGE_MS));
}

void display_show_state(enum display_state state)
{
	if (state != DISPLAY_RESULTS)
		k_timer_stop(&page_timer);
	switch (state) {
	/* ... your cases ... */
	}
}

