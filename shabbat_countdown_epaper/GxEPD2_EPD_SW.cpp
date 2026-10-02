// Vendored / patched copy of GxEPD2_EPD (sw-spi variant) — see GxEPD2_EPD_SW.h
// for the full explanation of why this file exists. This is a byte-for-byte
// copy of GxEPD2's own official "extras/sw_spi" GxEPD2_EPD.cpp, with only the
// class name changed (GxEPD2_EPD -> GxEPD2_EPD_SW).
//
// Original file this is based on: GxEPD2_EPD.cpp, "extras/sw_spi" variant
// Author: Jean-Marc Zingg · Library: https://github.com/ZinggJM/GxEPD2

#include "GxEPD2_EPD_SW.h"

#if defined(ESP8266) || defined(ESP32)
#include <pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif

GxEPD2_EPD_SW::GxEPD2_EPD_SW(int16_t cs, int16_t dc, int16_t rst, int16_t busy, int16_t busy_level, uint32_t busy_timeout,
                             uint16_t w, uint16_t h, GxEPD2::Panel p, bool c, bool pu, bool fpu) :
  WIDTH(w), HEIGHT(h), panel(p), hasColor(c), hasPartialUpdate(pu), hasFastPartialUpdate(fpu),
  _sck(-1), _mosi(-1),
  _cs(cs), _dc(dc), _rst(rst), _busy(busy), _busy_level(busy_level), _busy_timeout(busy_timeout), _diag_enabled(false),
  _spi_settings(4000000, MSBFIRST, SPI_MODE0)
{
  _initial_write = true;
  _initial_refresh = true;
  _power_is_on = false;
  _using_partial_mode = false;
  _hibernating = false;
  _init_display_done = false;
  _reset_duration = 20;
}

void GxEPD2_EPD_SW::init(uint32_t serial_diag_bitrate)
{
  init(serial_diag_bitrate, true, 20, false);
}

void GxEPD2_EPD_SW::init(uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration, bool pulldown_rst_mode)
{
  _initial_write = initial;
  _initial_refresh = initial;
  _pulldown_rst_mode = pulldown_rst_mode;
  _power_is_on = false;
  _using_partial_mode = false;
  _hibernating = false;
  _init_display_done = false;
  _reset_duration = reset_duration;
  if (serial_diag_bitrate > 0)
  {
    Serial.begin(serial_diag_bitrate);
    _diag_enabled = true;
  }
  if (_cs >= 0)
  {
    digitalWrite(_cs, HIGH);
    pinMode(_cs, OUTPUT);
  }
  if (_dc >= 0)
  {
    digitalWrite(_dc, HIGH);
    pinMode(_dc, OUTPUT);
  }
  _reset();
  if (_busy >= 0)
  {
    pinMode(_busy, INPUT);
  }
  if (_sck < 0) SPI.begin();
}

void GxEPD2_EPD_SW::init(int16_t sck, int16_t mosi, uint32_t serial_diag_bitrate, bool initial, uint16_t reset_duration, bool pulldown_rst_mode)
{
  if ((sck >= 0) && (mosi >= 0))
  {
    _sck = sck;
    _mosi = mosi;
    digitalWrite(_sck, LOW);
    digitalWrite(_mosi, LOW);
    pinMode(_sck, OUTPUT);
    pinMode(_mosi, OUTPUT);
  } else _sck = -1;
  init(serial_diag_bitrate, initial, reset_duration, pulldown_rst_mode);
}

void GxEPD2_EPD_SW::end()
{
  pinMode(_sck, INPUT);
  pinMode(_mosi, INPUT);
  if (_cs >= 0) pinMode(_cs, INPUT);
  if (_dc >= 0) pinMode(_dc, INPUT);
  if (_rst >= 0) pinMode(_rst, INPUT);
}

void GxEPD2_EPD_SW::_reset()
{
  if (_rst >= 0)
  {
    if (_pulldown_rst_mode)
    {
      digitalWrite(_rst, LOW);
      pinMode(_rst, OUTPUT);
      delay(_reset_duration);
      pinMode(_rst, INPUT_PULLUP);
      delay(200);
    }
    else
    {
      digitalWrite(_rst, HIGH);
      pinMode(_rst, OUTPUT);
      delay(20);
      digitalWrite(_rst, LOW);
      delay(_reset_duration);
      digitalWrite(_rst, HIGH);
      delay(200);
    }
    _hibernating = false;
  }
}

void GxEPD2_EPD_SW::_waitWhileBusy(const char* comment, uint16_t busy_time)
{
  if (_busy >= 0)
  {
    delay(1); // add some margin to become active
    unsigned long start = micros();
    while (1)
    {
      if (digitalRead(_busy) != _busy_level) break;
      delay(1);
      if (micros() - start > _busy_timeout)
      {
        Serial.println("Busy Timeout!");
        break;
      }
    }
    if (comment)
    {
#if !defined(DISABLE_DIAGNOSTIC_OUTPUT)
      if (_diag_enabled)
      {
        unsigned long elapsed = micros() - start;
        Serial.print(comment);
        Serial.print(" : ");
        Serial.println(elapsed);
      }
#endif
    }
    (void) start;
  }
  else delay(busy_time);
}

void GxEPD2_EPD_SW::_writeCommand(uint8_t c)
{
  _beginTransaction(_spi_settings);
  if (_dc >= 0) digitalWrite(_dc, LOW);
  if (_cs >= 0) digitalWrite(_cs, LOW);
  _spi_write(c);
  if (_cs >= 0) digitalWrite(_cs, HIGH);
  if (_dc >= 0) digitalWrite(_dc, HIGH);
  _endTransaction();
}

void GxEPD2_EPD_SW::_writeData(uint8_t d)
{
  _beginTransaction(_spi_settings);
  if (_cs >= 0) digitalWrite(_cs, LOW);
  _spi_write(d);
  if (_cs >= 0) digitalWrite(_cs, HIGH);
  _endTransaction();
}

void GxEPD2_EPD_SW::_writeData(const uint8_t* data, uint16_t n)
{
  _beginTransaction(_spi_settings);
  if (_cs >= 0) digitalWrite(_cs, LOW);
  for (uint8_t i = 0; i < n; i++)
  {
    _spi_write(*data++);
  }
  if (_cs >= 0) digitalWrite(_cs, HIGH);
  _endTransaction();
}

void GxEPD2_EPD_SW::_writeDataPGM(const uint8_t* data, uint16_t n, int16_t fill_with_zeroes)
{
  _beginTransaction(_spi_settings);
  if (_cs >= 0) digitalWrite(_cs, LOW);
  for (uint8_t i = 0; i < n; i++)
  {
    _spi_write(pgm_read_byte(&*data++));
  }
  while (fill_with_zeroes > 0)
  {
    _spi_write(0x00);
    fill_with_zeroes--;
  }
  if (_cs >= 0) digitalWrite(_cs, HIGH);
  _endTransaction();
}

void GxEPD2_EPD_SW::_writeDataPGM_sCS(const uint8_t* data, uint16_t n, int16_t fill_with_zeroes)
{
  _beginTransaction(_spi_settings);
  for (uint8_t i = 0; i < n; i++)
  {
    if (_cs >= 0) digitalWrite(_cs, LOW);
    _spi_write(pgm_read_byte(&*data++));
    if (_cs >= 0) digitalWrite(_cs, HIGH);
  }
  while (fill_with_zeroes > 0)
  {
    if (_cs >= 0) digitalWrite(_cs, LOW);
    _spi_write(0x00);
    fill_with_zeroes--;
    if (_cs >= 0) digitalWrite(_cs, HIGH);
  }
  _endTransaction();
}

void GxEPD2_EPD_SW::_writeCommandData(const uint8_t* pCommandData, uint8_t datalen)
{
  _beginTransaction(_spi_settings);
  if (_dc >= 0) digitalWrite(_dc, LOW);
  if (_cs >= 0) digitalWrite(_cs, LOW);
  _spi_write(*pCommandData++);
  if (_dc >= 0) digitalWrite(_dc, HIGH);
  for (uint8_t i = 0; i < datalen - 1; i++)  // sub the command
  {
    _spi_write(*pCommandData++);
  }
  if (_cs >= 0) digitalWrite(_cs, HIGH);
  _endTransaction();
}

void GxEPD2_EPD_SW::_writeCommandDataPGM(const uint8_t* pCommandData, uint8_t datalen)
{
  _beginTransaction(_spi_settings);
  if (_dc >= 0) digitalWrite(_dc, LOW);
  if (_cs >= 0) digitalWrite(_cs, LOW);
  _spi_write(pgm_read_byte(&*pCommandData++));
  if (_dc >= 0) digitalWrite(_dc, HIGH);
  for (uint8_t i = 0; i < datalen - 1; i++)  // sub the command
  {
    _spi_write(pgm_read_byte(&*pCommandData++));
  }
  if (_cs >= 0) digitalWrite(_cs, HIGH);
  _endTransaction();
}

void GxEPD2_EPD_SW::_startTransfer()
{
  _beginTransaction(_spi_settings);
  if (_cs >= 0) digitalWrite(_cs, LOW);
}

void GxEPD2_EPD_SW::_transfer(uint8_t value)
{
  _spi_write(value);
}

void GxEPD2_EPD_SW::_endTransfer()
{
  if (_cs >= 0) digitalWrite(_cs, HIGH);
  _endTransaction();
}

void GxEPD2_EPD_SW::_beginTransaction(const SPISettings& settings)
{
  if (_sck < 0) SPI.beginTransaction(settings);
}

void GxEPD2_EPD_SW::_spi_write(uint8_t data)
{
  if (_sck < 0) SPI.transfer(data);
  else
  {
#if defined (ESP8266)
    yield();
#endif
    for (int i = 0; i < 8; i++)
    {
      digitalWrite(_mosi, (data & 0x80) ? HIGH : LOW);
      data <<= 1;
      digitalWrite(_sck, HIGH);
      digitalWrite(_sck, LOW);
    }
  }
}

void GxEPD2_EPD_SW::_endTransaction()
{
  if (_sck < 0) SPI.endTransaction();
}

uint8_t GxEPD2_EPD_SW::_readData()
{
  uint8_t data = 0;
  _beginTransaction(_spi_settings);
  if (_cs >= 0) digitalWrite(_cs, LOW);
  if (_sck < 0)
  {
    data = SPI.transfer(0);
  }
  else
  {
    pinMode(_mosi, INPUT);
    for (int i = 0; i < 8; i++)
    {
      data <<= 1;
      digitalWrite(_sck, HIGH);
      data |= digitalRead(_mosi);
      digitalWrite(_sck, LOW);
    }
    pinMode(_mosi, OUTPUT);
  }
  if (_cs >= 0) digitalWrite(_cs, HIGH);
  _endTransaction();
  return data;
}

void GxEPD2_EPD_SW::_readData(uint8_t* data, uint16_t n)
{
  _beginTransaction(_spi_settings);
  if (_cs >= 0) digitalWrite(_cs, LOW);
  if (_sck < 0)
  {
    for (uint8_t i = 0; i < n; i++)
    {
      *data++ = SPI.transfer(0);
    }
  }
  else
  {
    pinMode(_mosi, INPUT);
    for (uint8_t i = 0; i < n; i++)
    {
      *data = 0;
      for (int i = 0; i < 8; i++)
      {
        *data <<= 1;
        digitalWrite(_sck, HIGH);
        *data |= digitalRead(_mosi);
        digitalWrite(_sck, LOW);
      }
      data++;
    }
    pinMode(_mosi, OUTPUT);
  }
  if (_cs >= 0) digitalWrite(_cs, HIGH);
  _endTransaction();
}
