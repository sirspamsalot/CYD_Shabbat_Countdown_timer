// Vendored / patched copy of GxEPD2_420_GDEY042T81, for this sketch only.
//
// This is a byte-for-byte copy of the real GxEPD2_420_GDEY042T81 panel
// driver class (same panel, same SSD1683 controller, same command
// sequences — nothing about the display protocol itself has changed),
// renamed GxEPD2_420_GDEY042T81_SW and re-pointed at GxEPD2_EPD_SW instead
// of the library's own GxEPD2_EPD. See GxEPD2_EPD_SW.h in this same folder
// for the full explanation of why that swap fixes "Busy Timeout!" on this
// board (bit-banged/software SPI instead of the ESP32 hardware SPI
// peripheral, matching Elecrow's own official working example).
//
// Original file this is based on:
// Display Library for SPI e-paper panels from Dalian Good Display and boards from Waveshare.
// Panel: GDEY042T81 : https://www.good-display.com/product/386.html
// Controller : SSD1683 : https://v4.cecdn.yun300.cn/100001_1909185148/SSD1683.PDF
// Author: Jean-Marc Zingg · Library: https://github.com/ZinggJM/GxEPD2

#ifndef _GxEPD2_420_GDEY042T81_SW_H_
#define _GxEPD2_420_GDEY042T81_SW_H_

#include "GxEPD2_EPD_SW.h"

class GxEPD2_420_GDEY042T81_SW : public GxEPD2_EPD_SW
{
  public:
    // attributes
    static const uint16_t WIDTH = 400;
    static const uint16_t WIDTH_VISIBLE = WIDTH;
    static const uint16_t HEIGHT = 300;
    static const GxEPD2::Panel panel = GxEPD2::GDEY042T81;
    static const bool hasColor = false;
    static const bool hasPartialUpdate = true;
    static const bool hasFastPartialUpdate = true;
    static const bool useFastFullUpdate = true; // set false for extended (low) temperature range
    static const uint16_t power_on_time = 100; // ms, e.g. 96000us
    static const uint16_t power_off_time = 300; // ms, e.g. 268000us
    static const uint16_t full_refresh_time = 1200; // ms, e.g. 1140000us
    static const uint16_t partial_refresh_time = 400; // ms, e.g. 358000us
    // constructor
    GxEPD2_420_GDEY042T81_SW(int16_t cs, int16_t dc, int16_t rst, int16_t busy);
    // methods (virtual)
    void clearScreen(uint8_t value = 0xFF);
    void writeScreenBuffer(uint8_t value = 0xFF);
    void writeScreenBufferAgain(uint8_t value = 0xFF);
    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                             int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void refresh(bool partial_update_mode = false);
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h);
    void powerOff();
    void hibernate();
  private:
    void _writeScreenBuffer(uint8_t command, uint8_t value);
    void _writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void _writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                         int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void _PowerOn();
    void _PowerOff();
    void _InitDisplay();
    void _Update_Full();
    void _Update_Part();
};

#endif
