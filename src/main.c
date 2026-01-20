#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(dev)) {
		printk("Display not ready\n");
		return 0;
	}

	printk("Display ready, turning on\n");
	display_blanking_off(dev);

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
