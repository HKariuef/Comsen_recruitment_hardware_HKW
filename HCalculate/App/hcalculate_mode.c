#include "hcalculate_mode.h"

#include "ttp229.h"

#define KEY_MODE 2U
#define KEY_UP 3U
#define KEY_OK 4U
#define KEY_ENTER 29U
#define KEY_DOWN 8U
#define STAT_MENU_INDEX 3U

static volatile uint8_t stats_mode;
static volatile uint8_t mode_changed;
static volatile uint8_t menu_changed;
static volatile uint8_t menu_open;
static volatile uint8_t menu_index;
static uint32_t held_keys;
static uint32_t suppressed_keys;
static uint32_t stat_transition_mask;
static uint32_t candidate_raw;
static uint8_t candidate_count;
static uint8_t release_ticks;
/* STAT consumes the normal calculator key stream.  Keep a small, independent
 * MODE detector so leaving STAT does not depend on the calculator's own
 * touch-filter state or on a stale suppressed_keys bit. */
static uint8_t stats_mode_mode_count;
static uint8_t stats_mode_mode_down;
static volatile uint8_t serial_lcd_write;

static void mark_menu_changed(void)
{
    menu_changed = 1U;
}

static void process_key_edges(uint32_t raw)
{
    uint32_t rising;
    uint32_t bit;
    uint8_t key;

    if (raw != candidate_raw) {
        candidate_raw = raw;
        candidate_count = 1U;
        return;
    }
    if (candidate_count < 2U) {
        ++candidate_count;
        return;
    }
    rising = raw & ~held_keys;

    if (raw == 0U) {
        if (release_ticks < 3U) ++release_ticks;
        if (release_ticks >= 2U) held_keys = 0U;
        if (release_ticks >= 2U) suppressed_keys = 0U;
        if (release_ticks >= 2U) stat_transition_mask = 0U;
        return;
    }
    release_ticks = 0U;
    if (rising == 0U) return;
    bit = 1UL;
    for (key = 0U; key < 30U; ++key, bit <<= 1U) {
        if ((rising & bit) == 0U) continue;
        if (key == KEY_MODE) {
            if (stats_mode != 0U) {
                /* MODE leaves STAT and returns to the original menu. */
                stats_mode = 0U;
                mode_changed = 1U;
                menu_open = 1U;
                mark_menu_changed();
            } else {
                menu_open = 1U;
                menu_index = 0U;
                /* Starting a fresh original MODE menu must never inherit a
                 * suppression bit from the synthetic STAT entry. */
                suppressed_keys = 0U;
                stat_transition_mask = 0U;
                mark_menu_changed();
            }
        } else if (menu_open != 0U) {
            if (key == KEY_DOWN) {
                if (menu_index < STAT_MENU_INDEX) {
                    ++menu_index;
                }
                mark_menu_changed();
            } else if (key == KEY_UP) {
                if (menu_index > 0U) {
                    if (menu_index == STAT_MENU_INDEX) {
                        stat_transition_mask |= (1UL << KEY_UP);
                    }
                    --menu_index;
                }
                mark_menu_changed();
            } else if ((key == KEY_OK || key == KEY_ENTER) &&
                       menu_index == STAT_MENU_INDEX) {
                stats_mode = 1U;
                mode_changed = 1U;
                menu_open = 0U;
                stats_mode_mode_count = 0U;
                stats_mode_mode_down = 0U;
                held_keys = 0U;
                suppressed_keys = 0U;
                stat_transition_mask = 0U;
                candidate_raw = 0U;
                candidate_count = 0U;
                release_ticks = 0U;
                suppressed_keys |= (1UL << KEY_OK);
                mark_menu_changed();
            } else if (key == KEY_OK && (menu_index == 0U || menu_index == 1U)) {
                menu_open = 0U;
                suppressed_keys = 0U;
                mark_menu_changed();
            } else if (key == KEY_OK && menu_index == 2U) {
                /* ANGLE UNIT has its own original DEG/RAD screen. Stop
                 * observing arrows until MODE is pressed again. */
                menu_open = 0U;
                suppressed_keys = 0U;
            }
        }
        break;
    }
    held_keys |= rising;
}

static void process_stats_mode_key(uint32_t raw)
{
    if ((raw & (1UL << KEY_MODE)) == 0U) {
        stats_mode_mode_count = 0U;
        stats_mode_mode_down = 0U;
        return;
    }

    if (stats_mode_mode_down != 0U) {
        return;
    }

    if (stats_mode_mode_count < 3U) {
        ++stats_mode_mode_count;
    }
    if (stats_mode_mode_count >= 2U) {
        /* Consume this MODE press and return to the same mode-selection page
         * used by the original application. */
        stats_mode_mode_down = 1U;
        stats_mode = 0U;
        mode_changed = 1U;
        menu_open = 1U;
        /* The original example remains on CMPLX while the synthetic STAT
         * item is active.  Resume from that same item so its menu state and
         * the extended menu stay synchronized. */
        menu_index = 2U;
        menu_changed = 1U;
        held_keys = 0U;
        suppressed_keys = 0U;
        stat_transition_mask = 0U;
        candidate_raw = raw;
        candidate_count = 1U;
        release_ticks = 0U;
    }
}

uint8_t hcalculate_stats_mode_active(void)
{
    return stats_mode;
}

void hcalculate_set_stats_mode(uint8_t active)
{
    active = active != 0U ? 1U : 0U;
    if (stats_mode != active) {
        stats_mode = active;
        mode_changed = 1U;
        stats_mode_mode_count = 0U;
        stats_mode_mode_down = 0U;
        if (active != 0U) {
            /* Any key used to confirm STAT must be released before MODE can
             * be recognized as the next command. */
            held_keys = 0U;
            suppressed_keys = 0U;
            stat_transition_mask = 0U;
            candidate_raw = 0U;
            candidate_count = 0U;
            release_ticks = 0U;
        }
    }
}

uint8_t hcalculate_stats_mode_take_changed(void)
{
    uint8_t changed = mode_changed;
    mode_changed = 0U;
    return changed;
}

uint8_t hcalculate_mode_take_menu_changed(void)
{
    uint8_t changed = menu_changed;
    menu_changed = 0U;
    return changed;
}

uint8_t hcalculate_mode_menu_open(void)
{
    return menu_open;
}

uint8_t hcalculate_mode_menu_index(void)
{
    return menu_index;
}

void __real_lcd1602_write_lines(const char line1[16], const char line2[16]);

void hcalculate_lcd_write_serial(const char line1[16], const char line2[16])
{
    serial_lcd_write = 1U;
    __real_lcd1602_write_lines(line1, line2);
    serial_lcd_write = 0U;
}

void __wrap_lcd1602_write_lines(const char line1[16], const char line2[16])
{
    /* In every original mode this is a transparent pass-through.  STAT is
     * the only mode that owns the LCD, and serial_text.c explicitly marks
     * its writes as allowed. */
    if (stats_mode == 0U || serial_lcd_write != 0U) {
        __real_lcd1602_write_lines(line1, line2);
    }
}

uint32_t __real_ttp229_read_physical(void);

uint32_t __wrap_ttp229_read_physical(void)
{
    uint32_t raw = __real_ttp229_read_physical();
    uint32_t returned = raw;
    uint8_t was_stats_mode = stats_mode;

    if (was_stats_mode != 0U) {
        process_stats_mode_key(raw);
        if (stats_mode != 0U) {
            process_key_edges(raw);
        }
    } else if (stats_mode_mode_down == 0U ||
               (raw & (1UL << KEY_MODE)) == 0U) {
        process_key_edges(raw);
    }

    /* Clear the one-shot guard after the physical MODE key is released. */
    if (was_stats_mode == 0U && stats_mode_mode_down != 0U) {
        process_stats_mode_key(raw);
    }

    /* Keep the original application untouched for every original mode. Only
     * the synthetic STAT item and its serial-only page consume key presses. */
    if (stats_mode != 0U) {
        returned = 0U;
    } else if (stats_mode_mode_down != 0U &&
               (raw & (1UL << KEY_MODE)) != 0U) {
        returned &= ~(1UL << KEY_MODE);
    } else if (menu_open != 0U && menu_index == STAT_MENU_INDEX) {
        returned = raw;
        returned &= ~(1UL << KEY_MODE);
        returned &= ~(1UL << KEY_DOWN);
        returned &= ~(1UL << KEY_UP);
        returned &= ~(1UL << KEY_OK);
    }
    /* Suppression is only needed for the synthetic STAT transition.  The
     * original menu and ANGLE/COMP/CMPLX pages must receive their raw keys. */
    if (stats_mode != 0U) {
        returned = 0U;
    }
    returned &= ~stat_transition_mask;
    return returned;
}

