/**
 * @file UC8151c.c
 * @author Arijit Sadhu (arijitsadhu@users.noreply.github.com)
 * @brief Driver for the pimoroni Inky pHAT B&W UC8151 module.
 *
 * Although the display is mounted in landspace format the hardware raster is in portrait.
 * X-Y axis have been swapped to landscape
 * Code does not flip the graphics from portrait to landscape so they have to be flipped manually before compilation.
 *
 * @version 0.1
 * @date 2024-03-05
 *
 * @copyright Copyright (c) 2024 Arijit Sadhu
 *
 */

/* INCLUDES ****************************************/

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "uc8151c.h"

/* MACROS ****************************************/

#define UC8151_SPI_PORT spi0

/*
 * Command registers available from the manufacturer documentation
 */
#define UC8151_PANEL_SETTING 0x00
#define UC8151_POWER_SETTING 0x01
#define UC8151_POWER_OFF 0x02
#define UC8151_POWER_OFF_SEQUENCE_SETTING 0x03
#define UC8151_POWER_ON 0x04
#define UC8151_POWER_ON_MEASURE 0x05
#define UC8161_BOOSTER_SOFT_START 0x06
#define UC8151_DEEP_SLEEP 0x07
#define UC8151_DATA_START_TRANSMISSION_1 0x10
#define UC8151_DATA_STOP 0x11
#define UC8151_DISPLAY_REFRESH 0x12
#define UC8151_DATA_START_TRANSMISSION_2 0x13
#define UC8151_PLL_CONTROL 0x30
#define UC8151_TEMPERATURE_SENSOR_COMMAND 0x40
#define UC8151_TEMPERATURE_SENSOR_CALIBRATION 0x41
#define UC8151_TEMPERATURE_SENSOR_WRITE 0x42
#define UC8151_TEMPERATURE_SENSOR_READ 0x43
#define UC8151_VCOM_AND_DATA_INTERVAL_SETTING 0x50
#define UC8151_LOW_POWER_DETECTION 0x51
#define UC8151_TCON_SETTING 0x60
#define UC8151_TCON_RESOLUTION 0x61
#define UC8151_SOURCE_AND_GATE_START_SETTING 0x62
#define UC8151_GET_STATUS 0x71
#define UC8151_AUTO_MEASURE_VCOM 0x80
#define UC8151_VCOM_VALUE 0x81
#define UC8151_VCM_DC_SETTING_REGISTER 0x82
#define UC8151_PARTIAL_WINDOW 0x90
#define UC8151_PARTIAL_IN 0x91
#define UC8151_PARTIAL_OUT 0x92
#define UC8151_PROGRAM_MODE 0xA0
#define UC8151_ACTIVE_PROGRAMMING 0xA1
#define UC8151_READ_OTP 0xA2
#define UC8151_POWER_SAVING 0xE3

/* TYPES ****************************************/

/**
 * @brief Interface pins with our standard defaults where appropriate
 *
 */
enum Pin {
    A = 12,
    B = 13,
    C = 14,
    D = 15,
    E = 11,
    UP = 15, // alias for D
    DOWN = 11, // alias for E
    USER = 23,
    CS = 17,
    CLK = 18,
    MOSI = 19,
    DC = 20,
    RESET = 21,
    BUSY = 26,
    VBUS_DETECT = 24,
    LED = 25,
    BATTERY = 29,
    ENABLE_3V3 = 10
};

/* FUNCTION PROTOTYPES ****************************************/

/* GLOBAL VARIABLES ****************************************/

/* LOCAL VARIABLES ****************************************/

static spi_inst_t* spi = UC8151_SPI_PORT;

/* LOCAL FUNCTIONS ****************************************/

static void uc8151_write(uint8_t command, uint8_t* data, size_t size)
{
    // Chip Select line LOW
    gpio_put(CS, 0);

    // Pull the command AND chip select lines LOW
    gpio_put(DC, 0); // command mode

    // Send data to the SPI register
    spi_write_blocking(spi, (const uint8_t*)&command, 1);

    if (data && size) {
        // CMD pin HIGH
        gpio_put(DC, 1); // data mode

        // Write data to the device SPI buffer
        spi_write_blocking(spi, data, size);
    }

    // Return chip select to HIGH
    gpio_put(CS, 1);
}

static void uc8151_fill(uint8_t command, uint8_t data, size_t size)
{
    // Chip Select line LOW
    gpio_put(CS, 0);

    // Pull the command AND chip select lines LOW
    gpio_put(DC, 0); // command mode

    // Send data to the SPI register
    spi_write_blocking(spi, (const uint8_t*)&command, 1);

    // CMD pin HIGH
    gpio_put(DC, 1); // data mode

    for (uint16_t i = 0; i < size; i++) {
        // Write data to the device SPI buffer
        spi_write_blocking(spi, &data, 1);
    }

    // Return chip select to HIGH
    gpio_put(CS, 1);
}

/**
 * @brief Waits until the display pulls the busy pin HIGH.
 *
 */
static void uc8151_busy_wait()
{
    // TODO: include a timeout
    while (!gpio_get(BUSY)) {
        sleep_ms(2);
    };
}

/* GLOBAL FUCNTIONS ****************************************/

/**
 * @brief Initialise the UC8151C controller to begin the display.
 *
 */
void uc8151_init()
{
    // configure spi interface and pins
    spi_init(spi, 12000000);

    // SET control pins for the display HIGH (they are active LOW)
    gpio_set_function(DC, GPIO_FUNC_SIO);
    gpio_set_dir(DC, GPIO_OUT);

    gpio_set_function(CS, GPIO_FUNC_SIO);
    gpio_set_dir(CS, GPIO_OUT);
    gpio_put(CS, 1);

    // Bring the RESX pin HIGH.
    gpio_set_function(RESET, GPIO_FUNC_SIO);
    gpio_set_dir(RESET, GPIO_OUT);
    gpio_put(RESET, 1);

    //**make sure to set the BUSY pin as INPUT in your main setup**
    gpio_set_function(BUSY, GPIO_FUNC_SIO);
    gpio_set_dir(BUSY, GPIO_IN);
    gpio_set_pulls(BUSY, true, false);

    gpio_set_function(CLK, GPIO_FUNC_SPI);
    gpio_set_function(MOSI, GPIO_FUNC_SPI);

    uc8151_reset();

    uc8151_write(UC8161_BOOSTER_SOFT_START, (uint8_t[]) { 0x17, 0x17, 0x17 }, 3);

    uc8151_write(UC8151_POWER_SETTING, (uint8_t[]) { 0x03, 0x00, 0x2B, 0x2B, 0x09 }, 5);

    uc8151_write(UC8151_POWER_ON, NULL, 0);
    uc8151_busy_wait();

    // RES_128x296 | FORMAT_BW | BOOSTER_ON | RESET_NONE | LUT_OTP | SHIFT_RIGHT | SCAN_DOWN
    uc8151_write(UC8151_PANEL_SETTING, (uint8_t[]) { 0b10010111 }, 1);

    uc8151_write(UC8151_TCON_SETTING, (uint8_t[]) { 0x22 }, 1);

    uc8151_write(UC8151_VCOM_AND_DATA_INTERVAL_SETTING, (uint8_t[]) { 0x9c }, 1);

    uc8151_write(UC8151_TCON_SETTING, (uint8_t[]) { 0x01 }, 1);
}

/**
 * @brief Reset the module, used to awaken the module from sleep.
 *
 */
void uc8151_reset()
{
    // Do this by cycling the reset pin
    gpio_put(RESET, 0);
    sleep_ms(10);
    gpio_put(RESET, 1);
    sleep_ms(10);
}

/**
 * @brief Sets the draw area on the display SRAM.
 * You can keep drawing to RAM and then use uc8151_refresh() to update the display.
 *
 * @param data
 * @param width
 * @param height
 * @param x
 * @param y
 */
void uc8151_draw_bitmap(uint8_t* data, uint16_t width, uint16_t height, uint16_t x, uint16_t y)
{
    // partial frame command
    uc8151_write(UC8151_PARTIAL_IN, NULL, 0);

    uc8151_write(UC8151_PARTIAL_WINDOW, (uint8_t[]) {
                                            // Start verical channel
                                            (y & 0b11111000), // Ignore the last 3 bits
                                            // End verical channel
                                            (((y & 0b11111000) + height - 1) | 0b111),
                                            // Start horizontal line [8]
                                            (x >> 8),
                                            // Start horizontal line [7:0]
                                            (x),
                                            // End horizontal line [8]
                                            ((x + width - 1) >> 8),
                                            // End horizontal line [7:0]
                                            ((x + width - 1)),
                                            // Scan inside + outside
                                            (0x01),
                                        },
        7);

    uc8151_write(UC8151_DATA_START_TRANSMISSION_2, data, (height * width) / 8);

    // End partial frame
    sleep_ms(2);
    uc8151_write(UC8151_PARTIAL_OUT, NULL, 0);
}

/**
 * @brief Draws a rectangle to the screen SRAM. A refresh command needs to be sent to update the display.
 *
 * @param x1
 * @param y1
 * @param x2
 * @param y2
 * @param colour
 */
void uc8151_fill_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t colour)
{
    uint16_t width = x2 - x1;
    uint16_t height = y2 - y1;

    // partial frame command
    uc8151_write(UC8151_PARTIAL_IN, NULL, 0);

    uc8151_write(UC8151_PARTIAL_WINDOW, (uint8_t[]) {
                                            // Start verical channel
                                            (y1 & 0b11111000), // Ignore the last 3 bits
                                            // End verical channel
                                            (((y1 & 0b11111000) + height - 1) | 0b111),
                                            // Start horizontal line [8]
                                            (x1 >> 8),
                                            // Start horizontal line [7:0]
                                            (x1),
                                            // End horizontal line [8]
                                            ((x1 + width - 1) >> 8),
                                            // End horizontal line [7:0]
                                            ((x1 + width - 1)),
                                            // Scan inside + outside
                                            (0x01),
                                        },
        7);

    uc8151_fill(UC8151_DATA_START_TRANSMISSION_2, colour, width * height / 8);

    // End partial frame
    sleep_ms(2);
    uc8151_write(UC8151_PARTIAL_OUT, NULL, 0);
}

/**
 * @brief Clears the frame data from the display RAM
 *
 */
void uc8151_clear()
{
    uc8151_fill(UC8151_DATA_START_TRANSMISSION_2, 0xFF, UC8151_WIDTH * UC8151_HEIGHT / 8);
}

/**
 * @brief Update the display RAM with a bitmap
 *
 */
void uc8151_update(uint8_t* data)
{
    uc8151_write(UC8151_DATA_START_TRANSMISSION_2, data, UC8151_WIDTH * UC8151_HEIGHT / 8);
}

/**
 * @brief Puts the data in RAM on to the display.
 * @brief Puts the data in RAM on to the display. 
 * (Refreshes the display)
 */
void uc8151_refresh()
{
    uc8151_write(UC8151_DISPLAY_REFRESH, NULL, 0);
    sleep_ms(100);
    uc8151_busy_wait();
}

/**
 * @brief Put the display to sleep.
 * 
 * Wake the device using uc8151_init()
 */
void uc8151_sleep()
{
    uc8151_write(UC8151_POWER_OFF, NULL, 0);
    uc8151_busy_wait();
    uc8151_write(UC8151_DEEP_SLEEP, (uint8_t[]) { 0xA5 }, 1);
}