// CrowpanelEPD — minimal, direct port of Elecrow's own OFFICIAL, PROVEN-WORKING
// e-paper driver for this exact board (CrowPanel ESP32-S3 4.2" e-paper HMI).
//
// WHY THIS FILE EXISTS (read this before touching anything e-paper-related):
// Two earlier attempts to drive this panel both produced "Busy Timeout!" on
// every refresh:
//   1. The stock GxEPD2 library's GDEY042T81 driver, over the ESP32's
//      hardware SPI peripheral.
//   2. The same GxEPD2 driver, switched to bit-banged ("software") SPI —
//      which matched Elecrow's own example in HOW bytes get sent, but still
//      timed out, because it ALSO kept using GxEPD2's own init/refresh
//      REGISTER sequence (commands 0x24/0x26/0x20/0x12 — the newer,
//      "SSD1683-style" convention).
// Reading Elecrow's own working example (4.2_Example3_PWR, from
// https://github.com/Elecrow-RD/CrowPanel-ESP32-4.2-E-paper-HMI-Display-with-400-300)
// line by line turned up the real mismatch: the panel/controller in this
// board expects an OLDER register convention — init via commands
// 0x00/0x01/0x06/0x30/0x61/0x82/0x50/0x60/0xE3, writing the image to
// register 0x13 (not 0x24), and triggering the actual refresh with
// 0x17/0xA5 (not 0x20). GxEPD2's generic GDEY042T81 driver never sends any
// of that — it assumes a different controller generation entirely — so no
// amount of SPI-transport fixing was ever going to make it work.
//
// So instead of layering more patches onto GxEPD2, this file is a direct,
// deliberately minimal port of Elecrow's own EPD.cpp / EPD_SPI.cpp (the
// files behind that proven-working example), trimmed to exactly what this
// sketch needs: init the controller, and push a monochrome 400x300 buffer
// to the screen. All of the actual drawing (text, rectangles, icons) still
// happens in software, using Adafruit_GFX's GFXcanvas1 as a 400x300
// in-memory 1-bit canvas — see the top of the .ino for how `canvas` is
// declared and used. This file's only job is turning canvas.getBuffer()
// into pixels on the actual panel.
//
// Buffer convention: 1 bit = white, 0 bit = black (matches Elecrow's own
// EPD_Clear(), which fills both RAM buffers with 0xFF for an all-white
// screen) — the sketch's EPD_WHITE/EPD_BLACK constants are defined to match.
//
// NOTE ON SPEED: this only implements the ONE display path Elecrow's demo
// actually exercises and that is confirmed to work on real hardware: a full
// 400x300 buffer push using the "GC" lookup table (crowEpdDisplay()). A
// faster, partial-screen "DU" lookup table exists in the controller and in
// the LUT tables below (crowEpdDisplayPartial()) but is NOT used by
// Elecrow's own demo and has NOT been verified on this hardware — it's left
// here, unused, for future experimentation ONLY once the basic full-buffer
// path above is confirmed solid on your unit. Every screen in this sketch
// currently calls crowEpdDisplay() (the proven path), so refreshes are a
// full-panel flash every time rather than a fast partial update.
//
// Original files this is based on: EPD.h/.cpp, EPD_SPI.h/.cpp from
// Elecrow-RD/CrowPanel-ESP32-4.2-E-paper-HMI-Display-with-400-300,
// example/arduino_A_green_circular_sticker_on_the_back/Examples/4.2_Example3_PWR

#ifndef _CROWPANEL_EPD_H_
#define _CROWPANEL_EPD_H_

#include <Arduino.h>

#define CROW_EPD_W 400
#define CROW_EPD_H 300
#define CROW_EPD_BUFFER_BYTES ((CROW_EPD_W / 8) * CROW_EPD_H) // 15000

// One-time setup: configures CS/DC/RST/SCLK/MOSI as outputs and BUSY as an
// input, resets the panel, and sends the full register init sequence. Call
// this once from setup(), AFTER driving EPD_PWR high and letting it settle.
void crowEpdInit();

// Pushes a full CROW_EPD_BUFFER_BYTES (15000-byte) monochrome buffer to the
// panel and triggers a full refresh, using the exact sequence confirmed
// working on real hardware (EPD_Display_Fast + lut_GC + EPD_Update in
// Elecrow's own code). Blocks until the panel reports not-busy or ~10s
// elapses (prints "Busy Timeout!" if so, matching the diagnostic this
// sketch has used throughout its e-paper bring-up).
void crowEpdDisplay(const uint8_t *buffer);

// Same buffer push, but loads the "DU" lookup table instead of "GC" —
// Elecrow's own EPD_Display_Part() does this for a faster, lower-ghosting
// update. UNVERIFIED on this hardware (Elecrow's demo never calls it) — do
// not switch to this until crowEpdDisplay() above is confirmed reliable.
void crowEpdDisplayPartial(const uint8_t *buffer);

#endif
