#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <math.h>
#include <string.h>

#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 64

/* Framebuffer for monochrome display (1 bit per pixel, 8 pixels per byte) */
static uint8_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];

static void clear_framebuffer(void)
{
	memset(framebuffer, 0, sizeof(framebuffer));
}

/* Set a pixel in the framebuffer (SSD1306 uses vertical byte orientation) */
static void set_pixel(int x, int y)
{
	if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
		return;
	}
	/* SSD1306 stores 8 vertical pixels per byte */
	framebuffer[x + (y / 8) * DISPLAY_WIDTH] |= (1 << (y % 8));
}

/* Draw a line between two points using Bresenham's algorithm */
static void draw_line(int x0, int y0, int x1, int y1)
{
	int dx = abs(x1 - x0);
	int dy = -abs(y1 - y0);
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;

	while (1) {
		set_pixel(x0, y0);
		if (x0 == x1 && y0 == y1) {
			break;
		}
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

/* Draw a sine wave across the display */
static void draw_sine_wave(float phase, float amplitude, float frequency)
{
	int prev_y = -1;
	int prev_x = -1;

	for (int x = 0; x < DISPLAY_WIDTH; x++) {
		/* Calculate sine value, centered vertically on display */
		float angle = (float)x * frequency * 2.0f * 3.14159f / DISPLAY_WIDTH + phase;
		int y = (int)(DISPLAY_HEIGHT / 2 + amplitude * sinf(angle));

		/* Clamp y to display bounds */
		if (y < 0) y = 0;
		if (y >= DISPLAY_HEIGHT) y = DISPLAY_HEIGHT - 1;

		/* Draw line from previous point for smooth curve */
		if (prev_x >= 0) {
			draw_line(prev_x, prev_y, x, y);
		} else {
			set_pixel(x, y);
		}

		prev_x = x;
		prev_y = y;
	}
}

/* Update the display with framebuffer contents */
static void update_display(const struct device *dev)
{
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(framebuffer),
		.width = DISPLAY_WIDTH,
		.height = DISPLAY_HEIGHT,
		.pitch = DISPLAY_WIDTH,
	};

	display_write(dev, 0, 0, &desc, framebuffer);
}

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(dev)) {
		printk("Display not ready\n");
		return 0;
	}

	printk("Display ready, turning on\n");
	display_blanking_off(dev);

	float phase = 0.0f;

	while (1) {
		clear_framebuffer();

		/* Draw animated sine wave */
		draw_sine_wave(phase, 25.0f, 2.0f);

		update_display(dev);

		/* Animate by incrementing phase */
		phase += 0.1f;
		if (phase > 2.0f * 3.14159f) {
			phase -= 2.0f * 3.14159f;
		}

		k_sleep(K_MSEC(50));
	}

	return 0;
}
