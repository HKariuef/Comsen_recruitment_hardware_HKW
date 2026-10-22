#ifndef HCALCULATE_MODE_H
#define HCALCULATE_MODE_H

#include <stdint.h>

uint8_t hcalculate_stats_mode_active(void);
void hcalculate_set_stats_mode(uint8_t active);
uint8_t hcalculate_stats_mode_take_changed(void);
uint8_t hcalculate_mode_take_menu_changed(void);
uint8_t hcalculate_mode_menu_open(void);
uint8_t hcalculate_mode_menu_index(void);

/* LCD writes issued by the serial/stat page.  While STAT is active, the
 * original calculator LCD task is suppressed so it cannot overwrite input. */
void hcalculate_lcd_write_serial(const char line1[16], const char line2[16]);

#endif
