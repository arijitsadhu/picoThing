/**
 * @file bm.c
 * @author Arijit Sadhu (arijitsadhu@users.noreply.github.com)
 * @brief Simple bitmap handling library
 *
 * Draws BMP fsdata files, XBM raw bitmaps, prints text from fronts in BMP and XBM and draws QR codes from text
 *
 * @version 0.1
 * @date 2024-03-05
 *
 * @copyright Copyright (c) 2024 Arijit Sadhu
 *
 */

/* INCLUDES ****************************************/

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lwip/apps/fs.h"
#include "pico/stdlib.h"

#include "bm.h"
#include "qrcodegen.h"

/* MACROS ****************************************/

#define BM_HTTP_HEADER_SIZE (100)
#define BM_TEXT_SIZE (80)
#define BM_QR_SIZE (2048)
#define BM_QR_VERSION_MIN (qrcodegen_VERSION_MIN)
#define BM_QR_VERSION_MAX (11)

/* TYPES ****************************************/

/**
 * @brief BMP binary header format
 *
 */
typedef struct {
    uint16_t type; ///< Magic identifier: 0x4d42
    uint32_t size; ///< File size in bytes
    uint16_t reserved1; ///< Not used
    uint16_t reserved2; ///< Not used
    uint32_t offset; ///< Offset to image data in bytes from beginning of file
    uint32_t dib_header_size; ///< DIB Header size in bytes
    int32_t width_px; ///< Width of the image
    int32_t height_px; ///< Height of image
    uint16_t num_planes; ///< Number of color planes
    uint16_t bits_per_pixel; ///< Bits per pixel
    uint32_t compression; ///< Compression type
    uint32_t image_size_bytes; ///< Image size in bytes
    int32_t x_resolution_ppm; ///< Pixels per meter
    int32_t y_resolution_ppm; ///< Pixels per meter
    uint32_t num_colors; ///< Number of colors
    uint32_t important_colors; ///< Important colors
} __attribute__((packed)) bmp_header_t;

/* FUNCTION PROTOTYPES ****************************************/

/* GLOBAL VARIABLES ****************************************/

/* LOCAL VARIABLES ****************************************/

/**
 * @brief external bitmap drawing implementation
 *
 */
static bm_draw_cbk_t bm_draw_cbk = NULL;

/* LOCAL FUNCTIONS ****************************************/

/* GLOBAL FUCNTIONS ****************************************/

/**
 * @brief Initialized bitmap library with drawing implementation callback
 *
 * @param cbk external drawing implementation
 * @return true
 * @return false
 */
bool bm_init(bm_draw_cbk_t cbk)
{
    bool err = true;
    if (!cbk) {
        printf("Invalid callback\n");
    } else {
        bm_draw_cbk = cbk;
        err = false;
    }
    return err;
}

/**
 * @brief Draw a single pixel
 *
 * @param bm
 * @param width
 * @param height
 * @param x
 * @param y
 * @param val
 * @return true
 * @return false
 */
bool bm_draw_pixel(uint8_t* bm, uint16_t width, uint16_t height, uint16_t x, uint16_t y, bool val)
{
    bool err = true;
    if (!bm) {
        printf("Invalid bitmap\n");
    } else {
        if (val) {
            bm[(x * (height / 8)) + (y / 8)] &= ~(0b10000000 >> (y & 0b111));
        } else {
            bm[(x * (height / 8)) + (y / 8)] |= 0b10000000 >> (y & 0b111);
        }
        err = false;
    }
    return err;
}

/**
 * @brief Draw text from bitmap
 *
 * @param bm
 * @param char_w
 * @param char_h
 * @param x
 * @param y
 * @param str
 * @return true
 * @return false
 */
bool bm_draw_string(uint8_t* bm, uint8_t char_w, uint8_t char_h, uint16_t x, uint16_t y, char* str)
{
    uint8_t col = 0;
    bool err = true;

    if (!bm) {
        printf("Invalid bitmap\n");
    } else if (!bm_draw_cbk) {
        printf("Not initiliazed\n");
    } else {
        // Iterate through the string until terminator flag
        while (str[col] != '\0') {
            bm_draw_cbk(&bm[(str[col] - 32) * char_w * char_h / 8], char_w, char_h, (col * char_w) + x, y);
            col++;
        }
        err = false;
    }
    return err;
}

/**
 * @brief
 *
 * @param bm
 * @param width
 * @param height
 * @param x
 * @param y
 * @param fmt
 * @param ...
 * @return true
 * @return false
 */
bool bm_printf(uint8_t* bm, uint16_t width, uint16_t height, uint16_t x, uint16_t y, const char* fmt, ...)
{
    bool err = true;
    if (!bm || !fmt) {
        printf("Invalid bitmap or format\n");
    } else {
        char text[BM_TEXT_SIZE] = "";
        va_list args;
        va_start(args, fmt);
        vsnprintf(text, BM_TEXT_SIZE, fmt, args);
        va_end(args);
        err = bm_draw_string(bm, width / 95, height, x, y, text);
    }
    return err;
}

/**
 * @brief Load BMP fsdata file into a bitmap
 *
 * @param name
 * @param width
 * @param height
 * @return uint8_t*
 */
uint8_t* bmp_read(const char* name, uint16_t* width, uint16_t* height)
{
    struct fs_file file;
    uint8_t* bm = NULL;
    if (!name) {
        printf("Invalid bmp filename\n");
    } else {
        if (ERR_OK == fs_open(&file, name)) {
            bmp_header_t* header = (bmp_header_t*)&file.data[BM_HTTP_HEADER_SIZE];
            if (0x4d42 == header->type && file.len - BM_HTTP_HEADER_SIZE == header->size && 1 == header->bits_per_pixel) {
                bm = (char*)&file.data[BM_HTTP_HEADER_SIZE + header->offset];
                if (!bm) {
                    printf("Invalid bmp file\n");
                } else {
                    if (width) {
                        *width = header->height_px;
                    }
                    if (height) {
                        *height = header->width_px;
                    }
                }
            }
        }
    }

    return bm;
}

/**
 * @brief Draw text from BMP fomnt
 *
 * @param name
 * @param x
 * @param y
 * @param fmt
 * @param ...
 * @return true
 * @return false
 */
bool bmp_printf(const char* name, uint16_t x, uint16_t y, const char* fmt, ...)
{
    bool err = true;
    if (!name || !fmt) {
        printf("Invalid bmp filename or format\n");
    } else {
        uint16_t width = 0;
        uint16_t height = 0;
        uint8_t* bm = bmp_read(name, &width, &height);
        if (!bm) {
            printf("Invalid bmp file\n");
        } else {
            char text[BM_TEXT_SIZE] = "";
            va_list args;
            va_start(args, fmt);
            vsnprintf(text, BM_TEXT_SIZE, fmt, args);
            va_end(args);
            err = bm_draw_string(bm, width / 95, height, x, y, text);
        }
    }
    return err;
}

/**
 * @brief Draw BMP fsdata file
 *
 * @param name
 * @param x
 * @param y
 * @return true
 * @return false
 */
bool bmp_draw(const char* name, uint16_t x, uint16_t y)
{
    bool err = true;
    if (!name) {
        printf("Invalid bmp filename\n");
    } else if (!bm_draw_cbk) {
        printf("Not initiliazed\n");
    } else {
        uint16_t width = 0;
        uint16_t height = 0;
        uint8_t* bm = bmp_read(name, &width, &height);
        if (!bm) {
            printf("Invalid bmp file\n");
        } else {
            bm_draw_cbk(bm, width, height, x, y);
            err = false;
        }
    }
    return err;
}

/**
 * @brief Draw QR code from text
 *
 * @param x
 * @param y
 * @param fmt
 * @param ...
 * @return uint16_t
 */
uint16_t bm_qr_printf(uint16_t x, uint16_t y, const char* fmt, ...)
{
    uint16_t new_size = 0;
    if (!fmt) {
        printf("Invalid format\n");
    } else if (!bm_draw_cbk) {
        printf("Not initiliazed\n");
    } else {
        char text[BM_TEXT_SIZE] = "";
        va_list args;
        va_start(args, fmt);
        vsnprintf(text, BM_TEXT_SIZE, fmt, args);
        va_end(args);

        // Text data
        uint8_t qr0[qrcodegen_BUFFER_LEN_MAX];
        uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];
        if (!qrcodegen_encodeText(text, tempBuffer, qr0, qrcodegen_Ecc_MEDIUM, BM_QR_VERSION_MIN, BM_QR_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
            printf("QR code error\n");
        } else {
            char bm[BM_QR_SIZE];
            uint16_t size = qrcodegen_getSize(qr0);
            new_size = ((size * 2) & 0xfff8) + 8;
            uint16_t border = new_size / 2 - size;
            memset(bm, 0xff, new_size * new_size / 8);
            for (uint16_t y = 0; y < size; y++) {
                for (uint16_t x = 0; x < size; x++) {
                    bool val = qrcodegen_getModule(qr0, x, y);
                    bm_draw_pixel(bm, new_size, new_size, x * 2 + border, y * 2 + border, val);
                    bm_draw_pixel(bm, new_size, new_size, x * 2 + border, y * 2 + 1 + border, val);
                    bm_draw_pixel(bm, new_size, new_size, x * 2 + 1 + border, y * 2 + border, val);
                    bm_draw_pixel(bm, new_size, new_size, x * 2 + 1 + border, y * 2 + 1 + border, val);
                }
            }
            bm_draw_cbk(bm, new_size, new_size, x, y);
        }
    }
    return new_size;
}