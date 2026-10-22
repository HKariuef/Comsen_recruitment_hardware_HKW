#include "hcalculate_math.h"

#include "hcalculate_mode.h"

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HC_MAX_SAMPLES 16U
#define HC_EPSILON     1.0e-7f

static void clear_line(char line[16])
{
    memset(line, ' ', 16U);
}

static void copy_line(char line[16], const char *text)
{
    size_t i;

    clear_line(line);
    for (i = 0U; i < 16U && text[i] != '\0'; ++i) {
        line[i] = text[i];
    }
}

static size_t put_unsigned(char *out, size_t capacity, uint32_t value)
{
    char reversed[10];
    size_t count = 0U;
    size_t i;

    do {
        if (count >= sizeof(reversed)) {
            break;
        }
        reversed[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    if (capacity == 0U) {
        return 0U;
    }
    if (count >= capacity) {
        count = capacity - 1U;
    }
    for (i = 0U; i < count; ++i) {
        out[i] = reversed[count - i - 1U];
    }
    out[count] = '\0';
    return count;
}

/* A small fixed-point formatter keeps printf float support out of the image. */
static void format_real(float value, char *out, size_t capacity)
{
    uint8_t negative = 0U;
    uint32_t scaled;
    uint32_t integer_part;
    uint32_t fraction;
    size_t used;

    if (capacity == 0U) {
        return;
    }
    if (isnan(value)) {
        strncpy(out, "nan", capacity - 1U);
        out[capacity - 1U] = '\0';
        return;
    }
    if (isinf(value)) {
        strncpy(out, value < 0.0f ? "-inf" : "inf", capacity - 1U);
        out[capacity - 1U] = '\0';
        return;
    }
    if (value > 4294967.0f) {
        strncpy(out, "ovf", capacity - 1U);
        out[capacity - 1U] = '\0';
        return;
    }
    if (value < 0.0f) {
        negative = 1U;
        value = -value;
    }

    scaled = (uint32_t)(value * 1000.0f + 0.5f);
    integer_part = scaled / 1000U;
    fraction = scaled % 1000U;
    used = 0U;
    if (negative && used + 1U < capacity) {
        out[used++] = '-';
    }
    used += put_unsigned(&out[used], capacity - used, integer_part);
    if (used + 1U < capacity) {
        out[used++] = '.';
        if (used + 3U < capacity) {
            out[used++] = (char)('0' + (fraction / 100U));
            out[used++] = (char)('0' + ((fraction / 10U) % 10U));
            out[used++] = (char)('0' + (fraction % 10U));
        }
    }
    /* Remove insignificant zeroes, but leave one digit after the point. */
    while (used > 0U && out[used - 1U] == '0' &&
           used > 1U && out[used - 2U] != '.') {
        --used;
    }
    out[used] = '\0';
}

static void format_complex_result(calc_status_t status,
                                  const calc_complex_t *result,
                                  char line1[16],
                                  char line2[16])
{
    char number[24];
    char text[32];

    if (status != CALC_OK) {
        switch (status) {
        case CALC_DOMAIN: copy_line(line1, "DOMAIN ERROR"); break;
        case CALC_DIV_ZERO: copy_line(line1, "DIV ZERO"); break;
        case CALC_SYNTAX: copy_line(line1, "SYNTAX ERROR"); break;
        default: copy_line(line1, "CALC ERROR"); break;
        }
        clear_line(line2);
        return;
    }

    format_real(result->real, number, sizeof(number));
    (void)strcpy(text, "Re=");
    (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    copy_line(line1, text);

    format_real(result->imag, number, sizeof(number));
    (void)strcpy(text, "Im=");
    (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    copy_line(line2, text);
}

static const char *skip_space(const char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text) != 0) {
        ++text;
    }
    return text;
}

/* Parse the small decimal grammar used by the terminal without pulling the
 * comparatively large newlib strtof implementation into the 64 KiB image. */
static int parse_float(const char *text, char **end, float *output)
{
    const char *p = text;
    float value = 0.0f;
    float scale = 1.0f;
    int sign = 1;
    int exponent = 0;
    int exponent_sign = 1;
    uint8_t digits = 0U;

    if (*p == '+' || *p == '-') {
        if (*p == '-') {
            sign = -1;
        }
        ++p;
    }
    while (*p >= '0' && *p <= '9') {
        value = value * 10.0f + (float)(*p - '0');
        ++p;
        digits = 1U;
    }
    if (*p == '.') {
        float place = 0.1f;
        ++p;
        while (*p >= '0' && *p <= '9') {
            value += (float)(*p - '0') * place;
            place *= 0.1f;
            ++p;
            digits = 1U;
        }
    }
    if (digits == 0U) {
        *end = (char *)text;
        return 0;
    }
    if (*p == 'e' || *p == 'E') {
        const char *exp_start = p++;
        if (*p == '+' || *p == '-') {
            if (*p == '-') {
                exponent_sign = -1;
            }
            ++p;
        }
        if (*p < '0' || *p > '9') {
            *end = (char *)exp_start;
            return 0;
        }
        while (*p >= '0' && *p <= '9') {
            if (exponent < 20) {
                exponent = exponent * 10 + (*p - '0');
            }
            ++p;
        }
        exponent *= exponent_sign;
    }
    if (exponent > 0) {
        while (exponent-- > 0) {
            scale *= 10.0f;
        }
    } else {
        while (exponent++ < 0) {
            scale *= 0.1f;
        }
    }
    *output = (float)sign * value * scale;
    *end = (char *)p;
    return isfinite(*output) != 0;
}

static int starts_with_command(const char *line, const char *command,
                               const char **argument)
{
    size_t i = 0U;

    while (command[i] != '\0') {
        if (toupper((unsigned char)line[i]) !=
            toupper((unsigned char)command[i])) {
            return 0;
        }
        ++i;
    }
    if (line[i] != ' ' && line[i] != '\t') {
        return 0;
    }
    *argument = skip_space(&line[i]);
    return 1;
}

/* Parse a comma-separated vector and stop at the requested delimiter. */
static int parse_vector(const char **cursor, char delimiter,
                        float values[HC_MAX_SAMPLES], size_t *count)
{
    const char *p = skip_space(*cursor);
    size_t n = 0U;

    while (*p != '\0') {
        char *end;
        float value;

        if (n >= HC_MAX_SAMPLES) {
            return 0;
        }
        if (parse_float(p, &end, &value) == 0) {
            return 0;
        }
        values[n++] = value;
        p = skip_space(end);
        if (*p == ',') {
            p = skip_space(p + 1);
            continue;
        }
        if (*p == delimiter) {
            *cursor = p + 1;
            *count = n;
            return 1;
        }
        if (delimiter == '\0' && *p == '\0') {
            *cursor = p;
            *count = n;
            return 1;
        }
        return 0;
    }
    return 0;
}

hcalculate_stats_status_t hcalculate_linear_regression(const float *x,
                                                       const float *y,
                                                       uint8_t count,
                                                       float *slope,
                                                       float *intercept,
                                                       float *r_squared)
{
    uint8_t i;
    float mean_x = 0.0f;
    float mean_y = 0.0f;
    float denominator = 0.0f;
    float numerator = 0.0f;
    float total = 0.0f;
    float residual = 0.0f;

    if (x == NULL || y == NULL || slope == NULL || intercept == NULL ||
        r_squared == NULL || count < 2U || count > HC_MAX_SAMPLES) {
        return HC_STATS_BAD_INPUT;
    }
    for (i = 0U; i < count; ++i) {
        if (isfinite(x[i]) == 0 || isfinite(y[i]) == 0) {
            return HC_STATS_BAD_INPUT;
        }
        mean_x += x[i];
        mean_y += y[i];
    }
    mean_x /= (float)count;
    mean_y /= (float)count;
    for (i = 0U; i < count; ++i) {
        float dx = x[i] - mean_x;
        float dy = y[i] - mean_y;
        denominator += dx * dx;
        numerator += dx * dy;
        total += dy * dy;
    }
    if (denominator < HC_EPSILON) {
        return HC_STATS_X_CONSTANT;
    }
    *slope = numerator / denominator;
    *intercept = mean_y - *slope * mean_x;
    for (i = 0U; i < count; ++i) {
        float error = y[i] - (*slope * x[i] + *intercept);
        residual += error * error;
    }
    *r_squared = total < HC_EPSILON ?
                 (residual < HC_EPSILON ? 1.0f : 0.0f) :
                 1.0f - residual / total;
    if (*r_squared < 0.0f && *r_squared > -0.0005f) {
        *r_squared = 0.0f;
    }
    return HC_STATS_OK;
}

static void format_stats_result(const char *argument,
                                char line1[16], char line2[16])
{
    float x[HC_MAX_SAMPLES];
    float y[HC_MAX_SAMPLES];
    const char *cursor = argument;
    size_t nx = 0U;
    size_t ny = 0U;
    hcalculate_stats_status_t stats_status;
    float slope;
    float intercept;
    float r_squared;
    char a[24];
    char b[24];
    char r2[24];
    char text[40];

    if (!parse_vector(&cursor, ';', x, &nx) ||
        !parse_vector(&cursor, '\0', y, &ny) ||
        nx != ny || nx < 2U) {
        copy_line(line1, "STAT ERROR");
        copy_line(line2, "USE x;x pairs");
        return;
    }
    stats_status = hcalculate_linear_regression(x, y, (uint8_t)nx,
                                                &slope, &intercept,
                                                &r_squared);
    if (stats_status == HC_STATS_X_CONSTANT) {
        copy_line(line1, "STAT ERROR");
        copy_line(line2, "X VARIANCE 0");
        return;
    }
    if (stats_status != HC_STATS_OK) {
        copy_line(line1, "STAT ERROR");
        copy_line(line2, "BAD DATA");
        return;
    }

    format_real(slope, a, sizeof(a));
    format_real(intercept, b, sizeof(b));
    (void)strcpy(text, "y=");
    (void)strncat(text, a, sizeof(text) - strlen(text) - 1U);
    (void)strncat(text, "x", sizeof(text) - strlen(text) - 1U);
    if (intercept >= 0.0f) {
        (void)strncat(text, "+", sizeof(text) - strlen(text) - 1U);
    }
    (void)strncat(text, b, sizeof(text) - strlen(text) - 1U);
    copy_line(line1, text);

    format_real(r_squared, r2, sizeof(r2));
    (void)strcpy(text, "R2=");
    (void)strncat(text, r2, sizeof(text) - strlen(text) - 1U);
    (void)strncat(text, " n=", sizeof(text) - strlen(text) - 1U);
    {
        char n_text[8];
        (void)put_unsigned(n_text, sizeof(n_text), (uint32_t)nx);
        (void)strncat(text, n_text, sizeof(text) - strlen(text) - 1U);
    }
    copy_line(line2, text);
}

static int parse_xy(const char *argument, float x[HC_MAX_SAMPLES],
                    float y[HC_MAX_SAMPLES], size_t *count)
{
    const char *cursor = argument;
    size_t nx = 0U;
    size_t ny = 0U;

    if (!parse_vector(&cursor, ';', x, &nx) ||
        !parse_vector(&cursor, '\0', y, &ny) || nx != ny || nx < 2U) {
        return 0;
    }
    *count = nx;
    return 1;
}

static void format_desc_result(const char *argument, char line1[16],
                               char line2[16])
{
    float values[HC_MAX_SAMPLES];
    const char *cursor = argument;
    size_t count = 0U;
    float mean = 0.0f;
    float variance = 0.0f;
    float minimum;
    float maximum;
    char number[24];
    char text[40];
    uint8_t i;

    if (!parse_vector(&cursor, '\0', values, &count) || count == 0U) {
        copy_line(line1, "DESC ERROR");
        copy_line(line2, "USE x1,x2,...");
        return;
    }
    minimum = values[0];
    maximum = values[0];
    for (i = 0U; i < count; ++i) {
        mean += values[i];
        if (values[i] < minimum) minimum = values[i];
        if (values[i] > maximum) maximum = values[i];
    }
    mean /= (float)count;
    for (i = 0U; i < count; ++i) {
        float delta = values[i] - mean;
        variance += delta * delta;
    }
    variance /= (float)count;
    format_real(mean, number, sizeof(number));
    (void)strcpy(text, "M=");
    (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    (void)strncat(text, " N=", sizeof(text) - strlen(text) - 1U);
    {
        char n_text[8];
        (void)put_unsigned(n_text, sizeof(n_text), (uint32_t)count);
        (void)strncat(text, n_text, sizeof(text) - strlen(text) - 1U);
    }
    copy_line(line1, text);
    format_real(sqrtf(variance), number, sizeof(number));
    (void)strcpy(text, "SD=");
    (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    format_real(minimum, number, sizeof(number));
    (void)strncat(text, " MIN=", sizeof(text) - strlen(text) - 1U);
    (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    copy_line(line2, text);
    (void)maximum; /* MAX is available to the firmware extension API. */
}

static void format_corr_result(const char *argument, char line1[16],
                               char line2[16])
{
    float x[HC_MAX_SAMPLES];
    float y[HC_MAX_SAMPLES];
    size_t count;
    float slope;
    float intercept;
    float r_squared;
    float mean_x = 0.0f;
    float mean_y = 0.0f;
    float numerator = 0.0f;
    float denom_x = 0.0f;
    float denom_y = 0.0f;
    float r;
    char number[24];
    char text[40];
    uint8_t i;

    if (!parse_xy(argument, x, y, &count) ||
        hcalculate_linear_regression(x, y, (uint8_t)count, &slope,
                                     &intercept, &r_squared) != HC_STATS_OK) {
        copy_line(line1, "CORR ERROR");
        copy_line(line2, "BAD DATA");
        return;
    }
    for (i = 0U; i < count; ++i) { mean_x += x[i]; mean_y += y[i]; }
    mean_x /= (float)count;
    mean_y /= (float)count;
    for (i = 0U; i < count; ++i) {
        float dx = x[i] - mean_x;
        float dy = y[i] - mean_y;
        numerator += dx * dy;
        denom_x += dx * dx;
        denom_y += dy * dy;
    }
    r = denom_x < HC_EPSILON || denom_y < HC_EPSILON ? 0.0f :
        numerator / sqrtf(denom_x * denom_y);
    format_real(r, number, sizeof(number));
    (void)strcpy(text, "r=");
    (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    copy_line(line1, text);
    format_real(r_squared, number, sizeof(number));
    (void)strcpy(text, "R2=");
    (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    copy_line(line2, text);
}

static void format_predict_result(const char *argument, char line1[16],
                                  char line2[16])
{
    float x[HC_MAX_SAMPLES];
    float y[HC_MAX_SAMPLES];
    const char *cursor = argument;
    size_t nx = 0U;
    size_t ny = 0U;
    float query;
    float slope;
    float intercept;
    float r_squared;
    char *end;
    char number[24];
    char text[40];

    if (!parse_vector(&cursor, ';', x, &nx) ||
        !parse_vector(&cursor, ';', y, &ny) || nx != ny || nx < 2U ||
        parse_float(skip_space(cursor), &end, &query) == 0 ||
        *skip_space(end) != '\0' ||
        hcalculate_linear_regression(x, y, (uint8_t)nx, &slope, &intercept,
                                     &r_squared) != HC_STATS_OK) {
        copy_line(line1, "PRED ERROR");
        copy_line(line2, "USE x;y;xp");
        return;
    }
    format_real(slope * query + intercept, number, sizeof(number));
    (void)strcpy(text, "YHAT=");
    (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    copy_line(line1, text);
    format_real(slope, number, sizeof(number));
    (void)strcpy(text, "a=");
    (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    copy_line(line2, text);
}

static void format_quad_result(const char *argument, char line1[16],
                               char line2[16])
{
    float x[HC_MAX_SAMPLES];
    float y[HC_MAX_SAMPLES];
    float matrix[3][4] = {{0.0f, 0.0f, 0.0f, 0.0f},
                          {0.0f, 0.0f, 0.0f, 0.0f},
                          {0.0f, 0.0f, 0.0f, 0.0f}};
    float coefficient[3];
    float total = 0.0f;
    float residual = 0.0f;
    size_t count;
    uint8_t i;
    char number[24];
    char text[40];

    if (!parse_xy(argument, x, y, &count)) {
        copy_line(line1, "QUAD ERROR");
        copy_line(line2, "USE x;y pairs");
        return;
    }
    for (i = 0U; i < count; ++i) {
        float x2 = x[i] * x[i];
        float x3 = x2 * x[i];
        float x4 = x3 * x[i];
        matrix[0][0] += x4; matrix[0][1] += x3; matrix[0][2] += x2;
        matrix[1][0] += x3; matrix[1][1] += x2; matrix[1][2] += x[i];
        matrix[2][0] += x2; matrix[2][1] += x[i]; matrix[2][2] += 1.0f;
        matrix[0][3] += x2 * y[i]; matrix[1][3] += x[i] * y[i]; matrix[2][3] += y[i];
        total += y[i] * y[i];
    }
    for (i = 0U; i < 3U; ++i) {
        uint8_t pivot = i;
        uint8_t row;
        for (row = (uint8_t)(i + 1U); row < 3U; ++row)
            if (fabsf(matrix[row][i]) > fabsf(matrix[pivot][i])) pivot = row;
        if (fabsf(matrix[pivot][i]) < HC_EPSILON) {
            copy_line(line1, "QUAD ERROR"); copy_line(line2, "SINGULAR"); return;
        }
        if (pivot != i) {
            uint8_t col;
            for (col = i; col < 4U; ++col) {
                float swap = matrix[i][col]; matrix[i][col] = matrix[pivot][col]; matrix[pivot][col] = swap;
            }
        }
        for (row = (uint8_t)(i + 1U); row < 3U; ++row) {
            float factor = matrix[row][i] / matrix[i][i];
            uint8_t col;
            for (col = i; col < 4U; ++col) matrix[row][col] -= factor * matrix[i][col];
        }
    }
    for (i = 3U; i-- > 0U;) {
        float value = matrix[i][3];
        uint8_t col;
        for (col = (uint8_t)(i + 1U); col < 3U; ++col) value -= matrix[i][col] * coefficient[col];
        coefficient[i] = value / matrix[i][i];
    }
    for (i = 0U; i < count; ++i) {
        float error = y[i] - (coefficient[0] * x[i] * x[i] + coefficient[1] * x[i] + coefficient[2]);
        residual += error * error;
    }
    format_real(coefficient[0], number, sizeof(number));
    (void)strcpy(text, "a="); (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    format_real(coefficient[1], number, sizeof(number));
    (void)strncat(text, " b=", sizeof(text) - strlen(text) - 1U); (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    copy_line(line1, text);
    format_real(coefficient[2], number, sizeof(number));
    (void)strcpy(text, "c="); (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    (void)strncat(text, " R2=", sizeof(text) - strlen(text) - 1U);
    format_real(total < HC_EPSILON ? 1.0f : 1.0f - residual / total, number, sizeof(number));
    (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    copy_line(line2, text);
}

static void format_zscore_result(const char *argument, char line1[16],
                                 char line2[16])
{
    float values[HC_MAX_SAMPLES];
    const char *cursor = argument;
    size_t count = 0U;
    float mean = 0.0f;
    float variance = 0.0f;
    float maximum = 0.0f;
    uint8_t index = 0U;
    uint8_t i;
    char number[24];
    char text[40];

    if (!parse_vector(&cursor, '\0', values, &count) || count < 2U) {
        copy_line(line1, "ZSCORE ERROR"); copy_line(line2, "USE x1,x2,..."); return;
    }
    for (i = 0U; i < count; ++i) mean += values[i];
    mean /= (float)count;
    for (i = 0U; i < count; ++i) { float d = values[i] - mean; variance += d * d; }
    variance = sqrtf(variance / (float)count);
    if (variance < HC_EPSILON) { copy_line(line1, "ZSCORE ERROR"); copy_line(line2, "SD ZERO"); return; }
    for (i = 0U; i < count; ++i) {
        float z = fabsf((values[i] - mean) / variance);
        if (z > maximum) { maximum = z; index = i; }
    }
    format_real(maximum, number, sizeof(number));
    (void)strcpy(text, "MAXZ="); (void)strncat(text, number, sizeof(text) - strlen(text) - 1U);
    copy_line(line1, text);
    (void)strcpy(text, "INDEX=");
    (void)put_unsigned(&text[strlen(text)], sizeof(text) - strlen(text), (uint32_t)(index + 1U));
    copy_line(line2, text);
}

uint8_t hcalculate_process_line(const char *line,
                                char line1[16],
                                char line2[16],
                                calc_complex_t *answer)
{
    const char *argument;

    if (starts_with_command(line, "C", &argument) != 0 ||
        starts_with_command(line, "COMPLEX", &argument) != 0) {
        calc_complex_t result = {0.0f, 0.0f};
        calc_complex_t previous = {0.0f, 0.0f};
        calc_status_t status;

        if (answer != NULL) {
            previous = *answer;
        }
        status = calculator_evaluate(argument, CALC_ANGLE_DEG, 1U,
                                     previous, &result);
        if (status == CALC_OK && answer != NULL) {
            *answer = result;
        }
        format_complex_result(status, &result, line1, line2);
        return 1U;
    }
    if (starts_with_command(line, "STAT", &argument) != 0) {
        if (hcalculate_stats_mode_active() == 0U) {
            copy_line(line1, "STAT LOCKED");
            copy_line(line2, "PRESS MODE");
            return 1U;
        }
        format_stats_result(argument, line1, line2);
        return 1U;
    }
    if (hcalculate_stats_mode_active() == 0U &&
        (starts_with_command(line, "DESC", &argument) != 0 ||
         starts_with_command(line, "CORR", &argument) != 0 ||
         starts_with_command(line, "PRED", &argument) != 0 ||
         starts_with_command(line, "QUAD", &argument) != 0 ||
         starts_with_command(line, "ZSCORE", &argument) != 0)) {
        copy_line(line1, "STAT LOCKED");
        copy_line(line2, "PRESS MODE");
        return 1U;
    }
    if (hcalculate_stats_mode_active() != 0U) {
        if (starts_with_command(line, "DESC", &argument) != 0) {
            format_desc_result(argument, line1, line2); return 1U;
        }
        if (starts_with_command(line, "CORR", &argument) != 0) {
            format_corr_result(argument, line1, line2); return 1U;
        }
        if (starts_with_command(line, "PRED", &argument) != 0) {
            format_predict_result(argument, line1, line2); return 1U;
        }
        if (starts_with_command(line, "QUAD", &argument) != 0) {
            format_quad_result(argument, line1, line2); return 1U;
        }
        if (starts_with_command(line, "ZSCORE", &argument) != 0) {
            format_zscore_result(argument, line1, line2); return 1U;
        }
    }
    return 0U;
}
