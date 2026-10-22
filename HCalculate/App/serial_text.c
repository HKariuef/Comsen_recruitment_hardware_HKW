#include "serial_text.h"

#include "hcalculate_math.h"
#include "hcalculate_mode.h"
#include "lcd1602.h"
#include "usbd_cdc_if.h"

#include <stdio.h>
#include <string.h>

#define SERIAL_RING_SIZE 256U
#define TEXT_MAX         64U

static volatile uint8_t ring[SERIAL_RING_SIZE];
static volatile uint16_t ring_head;
static volatile uint16_t ring_tail;

static char text[TEXT_MAX];
static uint8_t text_len;
static uint8_t dirty;
static uint8_t ready_pending;
static calc_complex_t answer;

static char line1[16];
static char line2[16];
/* CDC keeps this pointer until the USB transfer completes, so it must not be
 * an automatic (stack) buffer. */
static uint8_t tx_buffer[128];

static void serial_transmit_text(const char *prefix, const char *text_value,
                                 const char *suffix)
{
    size_t length = 0U;

    if (prefix != NULL) {
        while (prefix[length] != '\0' && length < sizeof(tx_buffer) - 1U) {
            tx_buffer[length] = (uint8_t)prefix[length];
            ++length;
        }
    }
    if (text_value != NULL) {
        size_t i = 0U;
        while (text_value[i] != '\0' && length < sizeof(tx_buffer) - 1U) {
            tx_buffer[length++] = (uint8_t)text_value[i++];
        }
    }
    if (suffix != NULL) {
        size_t i = 0U;
        while (suffix[i] != '\0' && length < sizeof(tx_buffer) - 1U) {
            tx_buffer[length++] = (uint8_t)suffix[i++];
        }
    }
    if (length != 0U) {
        (void)CDC_Transmit_FS(tx_buffer, (uint16_t)length);
    }
}

static void serial_transmit_result(const char first_line[16],
                                   const char second_line[16])
{
    memcpy(tx_buffer, first_line, 16U);
    tx_buffer[16] = '\r';
    tx_buffer[17] = '\n';
    memcpy(&tx_buffer[18], second_line, 16U);
    tx_buffer[34] = '\r';
    tx_buffer[35] = '\n';
    (void)CDC_Transmit_FS(tx_buffer, 36U);
}

void serial_text_init(void)
{
    ring_head = 0U;
    ring_tail = 0U;
    text_len = 0U;
    text[0] = '\0';
    dirty = 0U;
    ready_pending = 1U;
    answer.real = 0.0f;
    answer.imag = 0.0f;
}

void serial_text_push(uint8_t byte)
{
    uint16_t next = (uint16_t)((ring_head + 1U) % SERIAL_RING_SIZE);

    if (next != ring_tail) {
        ring[ring_head] = byte;
        ring_head = next;
    }
}

static void text_append(char c)
{
    if (text_len >= (TEXT_MAX - 1U)) {
        memmove(text, &text[1], TEXT_MAX - 2U);
        text_len = (uint8_t)(TEXT_MAX - 2U);
    }
    text[text_len++] = c;
    text[text_len] = '\0';
}

static void redraw(void)
{
    char label[24];
    uint8_t start = 0U;
    uint8_t i;

    memset(line1, ' ', sizeof(line1));
    memset(line2, ' ', sizeof(line2));

    if (text_len > 16U) {
        start = (uint8_t)(text_len - 16U);
    }
    for (uint8_t i = 0U; (i < 16U) && ((start + i) < text_len); i++) {
        line1[i] = text[start + i];
    }

    (void)snprintf(label, sizeof(label), "USB RX %3u",
                   (unsigned int)text_len);
    for (i = 0U; (i < 16U) && (label[i] != '\0'); i++) {
        line2[i] = label[i];
    }
    hcalculate_lcd_write_serial(line1, line2);
}

void serial_text_poll(void)
{
    uint8_t changed = 0U;
    uint8_t command_result = 0U;
    char command_line1[16];
    char command_line2[16];

    if (hcalculate_stats_mode_take_changed() != 0U) {
        if (hcalculate_stats_mode_active() != 0U) {
            /* STAT starts with a fresh serial command line. */
            text_len = 0U;
            text[0] = '\0';
            dirty = 0U;
            hcalculate_lcd_write_serial("STAT MODE       ",
                                        "SERIAL INPUT    ");
            serial_transmit_text("STAT MODE ON\r\n", NULL, NULL);
        } else if (hcalculate_mode_menu_open() == 0U) {
        hcalculate_lcd_write_serial("CALC MODE       ",
                                    "READY           ");
            serial_transmit_text("STAT MODE OFF\r\n", NULL, NULL);
        } else {
            serial_transmit_text("STAT MODE OFF\r\n", NULL, NULL);
        }
    }

    if (hcalculate_mode_take_menu_changed() != 0U &&
        hcalculate_stats_mode_active() == 0U &&
        hcalculate_mode_menu_open() != 0U) {
        static const char *const menu_items[] = {
            "> COMP          ", "> CMPLX         ",
            "> ANGLE UNIT    ", "> STAT          "
        };
        uint8_t index = hcalculate_mode_menu_index();
        if (index > 3U) index = 0U;
        hcalculate_lcd_write_serial("SELECT MODE     ", menu_items[index]);
    }

    /* Do not announce during boot: the host may not have opened the CDC port
     * yet. Announce on the first received packet instead. */
    if (ring_tail != ring_head && ready_pending != 0U) {
        serial_transmit_text("HCalculate READY\r\n", NULL, NULL);
        ready_pending = 0U;
    }

    while (ring_tail != ring_head) {
        uint8_t byte = ring[ring_tail];
        ring_tail = (uint16_t)((ring_tail + 1U) % SERIAL_RING_SIZE);

        if ((byte == '\r') || (byte == '\n')) {
            if (text_len > 0U &&
                hcalculate_process_line(text, command_line1,
                                        command_line2, &answer) != 0U) {
                text_len = 0U;
                text[0] = '\0';
                command_result = 1U;
            } else if (text_len > 0U) {
                serial_transmit_text("RX: ", text, "\r\n");
            }
            changed = 1U;
        } else if ((byte == 0x08U) || (byte == 0x7FU)) {
            if (text_len > 0U) {
                text_len--;
                text[text_len] = '\0';
            }
            changed = 1U;
        } else if ((byte >= 32U) && (byte < 127U)) {
            text_append((char)byte);
            changed = 1U;
        }
    }

    if (changed != 0U) {
        dirty = 1U;
    }
    if (command_result != 0U) {
        hcalculate_lcd_write_serial(command_line1, command_line2);
        serial_transmit_result(command_line1, command_line2);
        dirty = 0U;
    } else if (dirty != 0U) {
        dirty = 0U;
        redraw();
    } else if (hcalculate_stats_mode_active() != 0U && text_len != 0U) {
        /* The original calculator LCD task can redraw its own frame after a
         * mode transition.  While STAT is active, keep the serial input page
         * visible until CR/LF produces a result.  This branch is restricted
         * to STAT and never changes the original calculator display path. */
        redraw();
    }
}
