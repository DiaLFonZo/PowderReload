#pragma once

#if defined(BENCH_LCD_ILI9341)
#define ILI9341_DRIVER
#define TFT_HEIGHT 320
#else
#define ST7789_DRIVER
#define TFT_HEIGHT 280
#endif
#define TFT_WIDTH 240

#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST 4
#define SPI_FREQUENCY 40000000
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
