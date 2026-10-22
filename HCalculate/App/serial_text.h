#ifndef SERIAL_TEXT_H
#define SERIAL_TEXT_H

#include <stdint.h>

void serial_text_init(void);

/* USB 中断里调用，只负责把字节放进环形缓冲。 */
void serial_text_push(uint8_t byte);

/* 默认任务里周期调用；有新数据时直接刷新 1602。 */
void serial_text_poll(void);

#endif
