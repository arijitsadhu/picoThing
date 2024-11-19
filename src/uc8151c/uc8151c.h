/**
 * @file UC8151c.h
 * @author Arijit Sadhu (arijitsadhu@users.noreply.github.com)
 * @brief Refer to .c file
 * @version 0.1
 * @date 2024-03-05
 *
 * @copyright Copyright (c) 2024 Arijit Sadhu
 *
 */
#ifndef __UC8151C_H__
#define __UC8151C_H__

// Hard-coded dimensions of the display
#define UC8151_WIDTH (296)
#define UC8151_HEIGHT (128)

void uc8151_setup();
void uc8151_init();
void uc8151_reset();
void uc8151_draw_bitmap(uint8_t* data, uint16_t width, uint16_t height, uint16_t x, uint16_t y);
void uc8151_fill_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t colour);
void uc8151_update(uint8_t* data);
void uc8151_clear();
void uc8151_refresh();
void uc8151_sleep();

#endif /* __UC8151C_H__ */
