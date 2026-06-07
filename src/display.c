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

