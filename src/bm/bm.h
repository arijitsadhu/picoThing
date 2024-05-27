/**
 * @file bm.h
 * @author Arijit Sadhu (arijitsadhu@users.noreply.github.com)
 * @brief Refer to .c file
 * @version 0.1
 * @date 2024-03-05
 *
 * @copyright Copyright (c) 2024 Arijit Sadhu
 *
 */

#ifndef __BM_H__
#define __BM_H__

typedef void (*bm_draw_cbk_t)(uint8_t* data, uint16_t width, uint16_t height, uint16_t x, uint16_t y);

bool bm_init(bm_draw_cbk_t cbk);
bool bm_draw_pixel(uint8_t* bm, uint16_t width, uint16_t height, uint16_t x, uint16_t y, bool val);
bool bm_draw_string(uint8_t* bm, uint8_t char_w, uint8_t char_h, uint16_t x, uint16_t y, char* str);
bool bm_printf(uint8_t* bm, uint16_t width, uint16_t height, uint16_t x, uint16_t y, const char* fmt, ...);
uint8_t* bmp_read(const char* name, uint16_t* width, uint16_t* height);
bool bmp_printf(const char* name, uint16_t x, uint16_t y, const char* fmt, ...);
bool bmp_draw(const char* name, uint16_t x, uint16_t y);
uint16_t bm_qr_printf(uint16_t x, uint16_t y, const char* fmt, ...);

#endif /* __BM_H__ */
