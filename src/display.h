#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

#define DISPLAY_W            128
#define DISPLAY_H            64

/* Results: pinned elements */
#define STAT_TYPE_X          1
#define STAT_TYPE_Y          1
#define STAT_TYPE_SUFFIX_X   9
#define STAT_TYPE_SUFFIX_Y   6
#define STAT_EAR_X           112
#define STAT_EAR_Y           3

/* Results: stat band rows */
#define STAT_LABEL_Y         0
#define STAT_VALUE_Y         6

/* Results: page-1 confidence, page-2 columns */
#define CONF_X               34
#define ECV_X                20
#define SA_X                 46
#define TPP_X                78

/* Results: graph geometry */
#define GRAPH_Y              16
#define GRAPH_H              47
#define GRAPH_Y_MAX_X100     180
#define GRAPH_P_MIN_DAPA     (-400)
#define GRAPH_P_MAX_DAPA     200

/* Results: page-indicator dots */
#define DOT1_X               108
#define DOT2_X               114
#define DOT_Y                18

/* Paging */
#define RESULTS_PAGE_MS      2500

/* Level Screen Geometry */
#define LEVEL_CX 		   64
#define LEVEL_CY 		   36
#define LEVEL_R 		   16
#define LEVEL_TOL 	   	   6
#define LEVEL_FLAT_DEG     3

struct tymp_point {
	int16_t pressure_dapa;     /* -400 .. +200 */
	int16_t admittance_x100;   /* 0.01 mL units */
};

struct tymp_results {
	char    type[3];           /* "A","As","Ad","B","C" */
	char    ear;               /* 'L' or 'R'            */
	int16_t confidence_x100;   /* 92 = 0.92             */
	int16_t ecv_x10;           /* 0.1 mL units          */
	int16_t sa_x100;           /* 0.01 mL units         */
	int16_t tpp_dapa;          /* daPa                  */
	struct tymp_point points[64];
	int     n_points;
};

enum display_state {
	DISPLAY_BOOT, DISPLAY_READY, DISPLAY_SEEKING_SEAL, DISPLAY_MEASURING,
	DISPLAY_RESULTS, DISPLAY_REARM, DISPLAY_SUMMARY, DISPLAY_WARNING,
	DISPLAY_ERROR, DISPLAY_SLEEP, DISPLAY_LEVEL,
};

int  display_init(void);
void display_show_state(enum display_state state);
void display_show_tymp(const struct tymp_results *res);
void display_advance_page(void);
void display_blank(void);

void display_set_battery(int pct);
void display_set_ear(char lr);          /* 'L' or 'R' */
void display_set_post(int index, bool ok);
void display_set_angle(int x_deg, int y_deg);

#endif
