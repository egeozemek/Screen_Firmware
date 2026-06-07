/* main.c - display test harness: cycles through all screen states on the DK */

#include "display.h"
#include <zephyr/kernel.h>
#include <string.h>          /* strncpy */

/* Build a fake measurement (Lorentzian peak) so we can exercise Results/Summary. */
static void make_demo(struct tymp_results *r, const char *type, char ear,
                      int16_t conf, int16_t ecv, int16_t sa, int16_t tpp,
                      int ppp, int peak_sa, int base, int width)
{
	strncpy(r->type, type, sizeof(r->type));
	r->ear = ear;
	r->confidence_x100 = conf;
	r->ecv_x10 = ecv;
	r->sa_x100 = sa;
	r->tpp_dapa = tpp;
	r->n_points = 64;
	int n = r->n_points;
	int peak = (400 + ppp) * (n - 1) / 600;
	for (int i = 0; i < n; i++) {
		r->points[i].pressure_dapa = (int16_t)(-400 + i * 600 / (n - 1));
		int d = i - peak;
		int k = (d > 0) ? 3 : 2;
		int v = peak_sa * width / (width + d * d * k);
		r->points[i].admittance_x100 = (int16_t)(v < base ? base : v);
	}
}

int main(void)
{
	if (display_init() != 0)
		return -1;                 /* panel not ready - bail */

	struct tymp_results left, right;
	make_demo(&left,  "A", 'L', 92, 11, 65,    0,    0, 65, 10, 50);  /* Type A */
	make_demo(&right, "C", 'R', 90, 11, 55, -150, -150, 55, 10, 50);  /* Type C */

	for (int i = 0; i < 5; i++) display_set_post(i, true);   /* boot checklist all OK */
	display_set_battery(85);

	while (1) {
		display_set_ear('L');
		display_show_state(DISPLAY_BOOT);          k_sleep(K_SECONDS(2));
		display_show_state(DISPLAY_READY);         k_sleep(K_SECONDS(2));
		display_show_state(DISPLAY_SEEKING_SEAL);  k_sleep(K_SECONDS(2));
		display_show_state(DISPLAY_MEASURING);     k_sleep(K_SECONDS(2));  /* blank */
		display_show_tymp(&left);                  k_sleep(K_SECONDS(6));  /* watch it page */
		display_show_state(DISPLAY_REARM);         k_sleep(K_SECONDS(2));

		display_set_ear('R');
		display_show_state(DISPLAY_SEEKING_SEAL);  k_sleep(K_SECONDS(2));
		display_show_state(DISPLAY_MEASURING);     k_sleep(K_SECONDS(2));
		display_show_tymp(&right);                 k_sleep(K_SECONDS(6));

		display_show_state(DISPLAY_SUMMARY);       k_sleep(K_SECONDS(3));
		display_show_state(DISPLAY_WARNING);       k_sleep(K_SECONDS(2));
		display_show_state(DISPLAY_ERROR);         k_sleep(K_SECONDS(2));
		display_show_state(DISPLAY_SLEEP);         k_sleep(K_SECONDS(2));
	}
}