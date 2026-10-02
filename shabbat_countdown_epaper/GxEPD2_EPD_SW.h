// Vendored / patched copy of GxEPD2_EPD, for this sketch only.
//
// WHY THIS FILE EXISTS:
// The installed GxEPD2 library (ZinggJM/GxEPD2) talks to the panel over the
// ESP32's hardware SPI peripheral. On this board (CrowPanel ESP32-S3 4.2"
// e-paper), that path reliably produces "Busy Timeout!" on every refresh —
// confirmed not to be a dead/defective panel, because Elecrow's own official
// example sketch (4.2_Example3_PWR, from their GitHub repo) successfully
// drives the SAME panel on the SAME board using the SAME pins, but talks to
// it with a hand-rolled, bit-banged ("software") SPI implementation instead
// of the hardware SPI peripheral — plain digitalWrite() calls toggling SCLK
// and MOSI directly, with no SPI.begin()/SPI.transfer() anywhere. That is
// the one real difference between "works" and "Busy Timeout!" on this unit.
//
// GxEPD2 actually ships an official, documented way to do exactly this — a
// modified GxEPD2_EPD class that falls back to bit-banged SPI whenever it's
// given SCK/MOSI pin numbers, instead of calling SPI.begin()/SPI.transfer().
// See https://github.com/ZinggJM/GxEPD2 (extras/sw_spi) and the comment in
// its own GxEPD2_EPD.h: "This is a modified class GxEPD2_EPD that allows to
// use SW SPI with GxEPD2". Its documented install method is to overwrite the
// library's own src/GxEPD2_EPD.h/.cpp in place — but that's a manual,
// per-machine step that silently reverts the moment GxEPD2 is reinstalled or
// updated, and nothing about the sketch itself would explain why the panel
// suddenly stopped working again. So instead, this is a byte-for-byte copy
// of that same official sw-spi-capable class, renamed GxEPD2_EPD_SW so it
// lives entirely in the sketch folder, can't collide with (or be silently
// undone by) the real installed GxEPD2_EPD, and travels with this sketch
// wherever it goes. Nothing about its logic has been changed — only the
// class name.
//
// Pair this with GxEPD2_420_GDEY042T81_SW.h/.cpp (also in this sketch
// folder), which is the panel driver re-pointed at this base class instead
// of the real one.
//
// Original file this is based on: GxEPD2_EPD.h, "extras/sw_spi" variant
// Author: Jean-Marc Zingg · Library: https://github.com/ZinggJM/GxEPD2

#ifndef _GxEPD2_EPD_SW_H_
#define _GxEPD2_EPD_SW_H_

#include <Arduino.h>
#include <SPI.h>

#include <GxEPD2.h>

#pragma GCC diagnostic ignored "-Wunused-parameter"

class GxEPD2_EPD_SW
{
  public:
    // attributes
    const uint16_t WIDTH;
    const uint16_t HEIGHT;
    const GxEPD2::Panel panel;
    const bool hasColor;
    const bool hasPartialUpdate;
    const bool hasFastPartialUpdate;
    // constructor
    GxEPD2_EPD_SW(int16_t cs, int16_t dc, int16_t rst, int16_t busy, int16_t busy_level, uint32_t busy_timeout,
                  uint16_t w, uint16_t h, GxEPD2::Panel p, bool c, bool pu, bool fpu);
    virtual void init(uint32_t serial_diag_bitrate = 0); // serial_diag_bitrate = 0 : disabled
    virtual void init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration = 20, bool pulldown_rst_mode = false);
    // the special sw-spi overload: pass real SCLK/MOSI pin numbers here BEFORE
    // calling display.init(...) — see the call site in this sketch's setup()
    // for the exact two-call sequence this needs.
    virtual void init(int16_t sck, int16_t mosi, uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration = 20, bool pulldown_rst_mode = false);
    virtual void end(); // release SPI and control pins
    //  Support for Bitmaps (Sprites) to Controller Buffer and to Screen
    virtual void clearScreen(uint8_t value) = 0; // init controller memory and screen (default white)
    virtual void writeScreenBuffer(uint8_t value) = 0; // init controller memory (default white)
    // write to controller memory, without screen refresh; x and w should be multiple of 8
    virtual void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false) = 0;
    virtual void writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false)
    {
      writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
    }
    virtual void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false) = 0;
    virtual void writeScreenBufferAgain(uint8_t value = 0xFF) // init controller memory (default white)
    {
      writeScreenBuffer(value);
    }
    virtual void writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false)
    {
      writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
    }
    virtual void writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                     int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false)
    {
      writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
    }
    virtual void refresh(bool partial_update_mode = false) = 0; // screen refresh from controller memory to full screen
    virtual void refresh(int16_t x, int16_t y, int16_t w, int16_t h) = 0; // screen refresh from controller memory, partial screen
    virtual void powerOff() = 0; // turns off generation of panel driving voltages, avoids screen fading over time
    virtual void hibernate() = 0; // turns powerOff() and sets controller to deep sleep for minimum power use, ONLY if wakeable by RST (rst >= 0)
    virtual void setPaged() {}; // for GxEPD2_154c paged workaround
    static inline uint16_t gx_uint16_min(uint16_t a, uint16_t b)
    {
      return (a < b ? a : b);
    };
    static inline uint16_t gx_uint16_max(uint16_t a, uint16_t b)
    {
      return (a > b ? a : b);
    };
    void selectSPI(SPIClass& spi, SPISettings spi_settings){};
  protected:
    void _reset();
    void _writeDataPGM(const uint8_t* data, uint16_t n, int16_t fill_with_zeroes = 0);
    void _writeDataPGM_sCS(const uint8_t* data, uint16_t n, int16_t fill_with_zeroes = 0);
    void _writeCommandData(const uint8_t* pCommandData, uint8_t datalen);
    void _writeCommandDataPGM(const uint8_t* pCommandData, uint8_t datalen);
    void _startTransfer();
    void _transfer(uint8_t value);
    void _endTransfer();
    void _beginTransaction(const SPISettings& settings);
    void _spi_write(uint8_t data);
    void _endTransaction();
  public:
    void _waitWhileBusy(const char* comment = 0, uint16_t busy_time = 5000);
    void _writeCommand(uint8_t c);
    void _writeData(uint8_t d);
    void _writeData(const uint8_t* data, uint16_t n);
    uint8_t _readData();
    void _readData(uint8_t* data, uint16_t n);
  protected:
    int16_t _sck, _mosi;
    int16_t _cs, _dc, _rst, _busy, _busy_level;
    uint32_t _busy_timeout;
    bool _diag_enabled, _pulldown_rst_mode;
    SPISettings _spi_settings;
    bool _initial_write, _initial_refresh;
    bool _power_is_on, _using_partial_mode, _hibernating;
    bool _init_display_done;
    uint16_t _reset_duration;
};

#endif
