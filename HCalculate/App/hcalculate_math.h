#ifndef HCALCULATE_MATH_H
#define HCALCULATE_MATH_H

#include "calculator_engine.h"

#include <stdint.h>

typedef enum
{
    HC_STATS_OK = 0,
    HC_STATS_BAD_INPUT,
    HC_STATS_X_CONSTANT
} hcalculate_stats_status_t;

/* Fit y = slope*x + intercept and return the coefficient of determination. */
hcalculate_stats_status_t hcalculate_linear_regression(const float *x,
                                                       const float *y,
                                                       uint8_t count,
                                                       float *slope,
                                                       float *intercept,
                                                       float *r_squared);

/*
 * Process one complete line received from the USB CDC terminal.
 *
 * The function returns non-zero when the line is an HCalculate command.  In
 * that case line1 and line2 are filled with exactly 16 display characters,
 * including trailing spaces.  Ordinary text returns zero and is left to the
 * normal serial text viewer.
 *
 * Commands:
 *   C <expression>                         complex-capable calculator
 *   STAT <x1>,<x2>,...;<y1>,<y2>,...        least-squares linear fit
 *   DESC <x1>,<x2>,...                     descriptive statistics
 *   CORR <x1>,...;<y1>,...                 Pearson correlation and R2
 *   PRED <x1>,...;<y1>,...;<xp>            linear prediction
 *   QUAD <x1>,...;<y1>,...                 quadratic least-squares fit
 *   ZSCORE <x1>,<x2>,...                    largest absolute z-score
 */
uint8_t hcalculate_process_line(const char *line,
                                char line1[16],
                                char line2[16],
                                calc_complex_t *answer);

#endif
