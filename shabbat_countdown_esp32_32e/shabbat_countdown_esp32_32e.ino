/*
  Shabbat Candle-Lighting / Havdalah Countdown — "Studio 60" style
  LCDWiki "4.0inch ESP32-32E Display" variant (ST7796, 480x320, resistive touch)

  ======================================================================
  This is the third hardware target for this project, adapted from the CYD
  build (itself adapted from the ESP32-S3/ES3C28P original). The solar math,
  settings menu structure, fonts, ShabbatCon color scheme, and background
  NTP retry are all identical to the other two variants — only the display
  driver, pins, and SPI wiring differ, because this board uses a different
  display controller (ST7796 instead of ILI9341) on a bigger 480x320 panel.

  Board: LCDWiki "4.0inch ESP32-32E Display" — see
  https://www.lcdwiki.com/4.0inch_ESP32-32E_Display
    MCU:          plain ESP32 (ESP32-D0WD-V3), NOT ESP32-S3 — select
                  "ESP32 Dev Module" as the board, same as the CYD variant.
    LCD driver:   ST7796, SPI, 480x320 (this sketch uses it in landscape)
                  CS=15  DC=2  SCLK=14  MOSI=13  MISO=12  BL=27 (PWM-dimmable)
                  Reset is tied to EN, no separate GPIO — pass -1.
    Touch driver: XPT2046, resistive, SPI — UNLIKE the CYD, this board's
                  touch controller SHARES the display's SCLK/MOSI/MISO bus
                  (only CS and IRQ are separate pins: CS=33  IRQ=36). Don't
                  carry over the CYD's "touch has its own bus" assumption —
                  here, both devices are initialized on the same SPIClass
                  object, just selected by their own CS lines.
    USB:          Standard USB-UART bridge (CH340 or similar) — plain
                  Serial works normally, no native-USB quirks to work around.

  OPTIONAL GPS MODULE (GY-GPS6MV2 / u-blox NEO-6M): choose "GPS MODULE" on
  the first-run screen, or Settings > GPS. With GPS on, the module is the
  ONLY source of the time and the location — no Wi-Fi, ZIP, city or manual
  clock — and the whole clock runs unattended. Wire it to the board's I2C
  connector, which breaks out two free GPIOs plus 3V3/GND (verify the pin
  order on YOUR board's silkscreen):
      GPS VCC -> 3V3        GPS GND -> GND
      GPS TX  -> IO25       (ESP RX, UART1)
      GPS RX  -> IO32       (ESP TX, optional — nothing is ever sent)
  9600 baud. Avoid IO1/IO3 (USB-serial). There's no battery-backed RTC, so
  every boot needs a fresh fix (a window or open sky). The time zone comes
  from the nearest city in cities.h. Picking a ZIP/city, SET TIME, or
  GPS: OFF returns to the normal online/offline modes. Diagnostics print as
  [gps] lines on the USB Serial Monitor at 115200 baud.

  DISPLAY DRIVER: Arduino's Library Manager doesn't carry a ready-made
  Adafruit_GFX-compatible ST7796 driver under a stable package name, so this
  folder vendors one directly — Adafruit_ST7796S_kbv.h/.cpp, sitting right
  next to this .ino (Arduino compiles .h/.cpp files in the sketch folder
  automatically, no separate library install needed for this one file pair).
  It's a real Adafruit_SPITFT subclass with the exact same usage pattern as
  Adafruit_ILI9341 in the CYD sketch — same constructor shape, same
  tft.fillRect/drawRect/print/etc. calls — so none of the drawing code below
  needed to change, only the class name, includes, and pin/bus setup. See
  that file's own header comment for where it came from.

  UPDATE — layout now fills the full 480x320 canvas: every screen (header,
  clock digits, settings menu, keypad, text keyboard, city list, etc.) is
  laid out against SCR_W/SCR_H/CX near the top of this file instead of the
  CYD's original hardcoded 320x240 coordinates, based on real-hardware
  feedback that the first pass only drew into the top-left 320x240 corner.

  UPDATE — touch is now calibrated from all four real corner taps. With
  TOUCH_DEBUG on, all four corners showed both axes reading backwards
  relative to the screen (largest raw X on the LEFT, largest raw Y at the
  TOP) rather than transposed — so TOUCH_INVERT_X and TOUCH_INVERT_Y are
  both set to 1, TOUCH_SWAP_XY stays 0, and TS_MINX/MAXX/MINY/MAXY near the
  touch driver code bracket the actual readings from all four corners (not
  guesses), with a little headroom. TOUCH_DEBUG itself is back off (0) for
  normal use — flip it to 1 again if touch ever needs re-checking on a
  different unit; see the README's Touch calibration section for the
  step-by-step process.

  Display style: big seven-segment-style digits on black, in the spirit of
  the countdown clock from "Studio 60 on the Sunset Strip"'s title
  sequence, recolored to ShabbatCon's blue/orange. Digits are hand-drawn
  from filled rectangles, not text.

  Candle-lighting/havdalah times are computed ON-DEVICE using a NOAA-style
  solar position formula. The only network dependency is a ONE-TIME lookup
  converting your ZIP code to latitude/longitude (api.zippopotam.us)
  whenever the ZIP changes, plus periodic NTP time sync (bounded/retried
  in the background — see attemptNtpSync() — never blocks forever if
  there's no connection).

  TIMEZONE NOTE: solar calculation gives sunset as a precise UTC instant,
  but displaying it as a human clock time needs your local UTC offset,
  which astronomy alone can't supply. The "SET TIME" menu option enters
  local time and derives/stores that offset by comparing it against the
  NTP-synced UTC clock. This does NOT auto-adjust for DST — re-run SET
  TIME after clocks change. Until set once, a rough longitude-based
  estimate is used.

  BEFORE YOU FLASH:
  1. Wi-Fi credentials no longer need to be set here — flash as-is, then
     enter your network name/password on the device itself from
     Settings > WI-FI (stored in this device's own flash). This also means
     a firmware image built for the public web flasher never needs your
     real Wi-Fi password baked into it — see the repo README.
  2. Libraries (Arduino Library Manager): Adafruit GFX Library,
     XPT2046_Touchscreen (by Paul Stoffregen), ArduinoJson.
     WiFi, HTTPClient, Preferences, SPI ship with the ESP32 core.
     (No ST7796 library to install — it's vendored in this sketch folder,
     see the note above.)
  3. Board: "ESP32 Dev Module" (NOT ESP32S3). Flash size 4MB, default
     partition scheme.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include "Adafruit_ST7796S_kbv.h"
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
// Rounder sans-serif fonts (bundled with Adafruit_GFX) used for all chrome
// text — menus, buttons, captions, keypad. The seven-segment countdown
// digits are hand-drawn from rectangles (drawSevenSegDigit), not text, so
// they're unaffected by font choice and keep their blocky LED look.
#include <math.h>
#include "cities.h" // 250 major world cities, for picking a location without a US ZIP code

// These are declared this early (right after the includes) because the
// Arduino build system auto-generates function prototypes and inserts them
// immediately after the last #include — before any type defined later in
// this file would otherwise be visible. Several functions take these enums
// as parameters, so they have to exist before that insertion point.
enum AppScreen { SCR_CLOCK, SCR_SETTINGS, SCR_KEYPAD, SCR_BRIGHTNESS, SCR_TEXTPAD, SCR_CITY_LIST, SCR_FIRST_RUN, SCR_CONFIRM_RESET };
enum KeypadPurpose { KP_ZIP, KP_HAVDALAH, KP_YEAR, KP_MONTH, KP_DAY, KP_HOUR, KP_MINUTE };
// Alphanumeric text entry (Wi-Fi SSID/password) is a separate keyboard
// screen from the numeric keypad above, since it needs letters and symbols.
enum TextEntryMode { TXT_LOWER, TXT_UPPER, TXT_SYMBOLS };
enum TextPurpose { TXT_SSID, TXT_PASSWORD };

// ============ USER CONFIG ============
// These are just the FALLBACK defaults used the very first time the device
// boots with nothing saved yet. Wi-Fi credentials can now be entered (and
// changed any time) from Settings > WI-FI on the touchscreen itself, stored
// in this device's own flash — so you no longer need to hardcode real
// credentials here at all.
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Minutes before sunset for candle lighting. Common values: 18 (most
// communities), 20, 22, 40 (Jerusalem). Check with your local shul if unsure.
const int CANDLE_LIGHTING_MINUTES = 18;

const char* DEFAULT_ZIP = "89169";        // used only if no ZIP has been saved yet
const int DEFAULT_HAVDALAH_OFFSET_MIN = 42; // "3 medium stars" — 42 min after sunset; adjustable in Settings
const int DEFAULT_BRIGHTNESS_PCT = 50;
// ======================================

// ---------- LCDWiki 4.0inch ESP32-32E Display pins ----------
#define LCD_CS   15
#define LCD_DC    2
#define LCD_RST  -1   // tied to EN, no separate GPIO
#define LCD_SCLK 14
#define LCD_MOSI 13
#define LCD_MISO 12
#define LCD_BL   27

// UNLIKE the CYD, this board's touch controller SHARES the display's
// SCLK/MOSI/MISO bus — only CS and IRQ are separate. There is no
// TOUCH_CLK/TOUCH_MOSI/TOUCH_MISO here; the touch object below is
// initialized on the SAME SPIClass as the display (see tftSPI/ts.begin()).
#define TOUCH_CS   33
#define TOUCH_IRQ  36

#define BL_PWM_CHANNEL 0
#define BL_PWM_FREQ    5000
#define BL_PWM_RES     8

// ---------- GPS module pins (optional GY-GPS6MV2 / u-blox NEO-6M) ----------
// The module plugs into the board's I2C connector, which breaks out two
// otherwise-free GPIOs plus 3V3 and GND. UART1 is remapped onto them.
//
//   GPS module pin   Board connector   ESP32 GPIO
//   --------------   ---------------   ----------
//   VCC              3V3               (power only)
//   GND              GND               (ground)
//   TX               IO25 pin          IO25  <- GPS_RX_PIN (ESP receives)
//   RX               IO32 pin          IO32  -> GPS_TX_PIN (ESP transmits; optional)
//
// Avoid IO1/IO3: those are the USB-serial (CH340) lines used for flashing
// and the Serial Monitor. Confirm the connector's pin order on your board's
// silkscreen before powering anything.
#define GPS_RX_PIN 25   // IO25 = I2C connector pin  <- GPS module's TX pin
#define GPS_TX_PIN 32   // IO32 = I2C connector pin  -> GPS module's RX pin (optional; nothing is ever sent)
#define GPS_BAUD   9600 // NEO-6M factory default
// ------------------------------------------------

// ---------- Full-canvas layout constants ----------
// This panel is 480x320 in landscape (tft.setRotation(1)) — noticeably
// wider AND taller than the CYD/ES3C28P sketches' 320x240. Every screen
// below is laid out against these two numbers (and CX, the horizontal
// center) instead of the CYD's original hardcoded 320x240 coordinates, so
// the UI now fills the whole physical panel instead of only its top-left
// 320x240 corner.
#define SCR_W 480
#define SCR_H 320
#define CX (SCR_W / 2)

// Wrench settings button (top-right of the header) and its touch target.
#define GEAR_X (SCR_W - 21)
#define GEAR_Y 15
#define GEAR_HIT_X0 (GEAR_X - 14)
#define GEAR_HIT_X1 (GEAR_X + 14)
#define GEAR_HIT_Y0 2
#define GEAR_HIT_Y1 28
// Right edge that header-time text right-aligns against, and the left edge
// of the region cleared before redrawing it (see drawHeaderTime()).
#define HDR_TIME_RIGHT (GEAR_HIT_X0 - 6)
#define HDR_TIME_LEFT  200
// ------------------------------------------------

// ---------- Countdown digit palette (Studio 60-style LED look) ----------
#define COLOR_BG      0x0000   // black
#define COLOR_SEG_ON  0x0393   // ShabbatCon blue (#00719F) — lit segment
#define COLOR_SEG_OFF 0x0062   // dim blue (~1/8 brightness) — unlit "ghost" segment
// -----------------------------------------------------------

// ---------- Surrounding chrome palette (header/captions/menus) ----------
// ShabbatCon brand colors, from shabbatcon.com's logo: teal-blue #00719F
// and burnt orange #D65F22. Panel/edge derive from the blue, accent/caption
// colors from the orange; general text is a soft blue-white for contrast
// against the dark panel. The blue LED countdown digits above are left as-is.
#define COLOR_PANEL      0x0926  // dark blue-teal (#0A2733), header/keypad button fill
#define COLOR_PANEL_EDGE 0x0393  // ShabbatCon blue  (#00719F), panel/button edges
#define COLOR_ACCENT     0xD2E4  // ShabbatCon orange (#D65F22) — borders, dividers, captions
#define COLOR_ACCENT_DIM 0x6962  // dim orange (#6B2F11) — secondary captions
#define COLOR_INFO       0xCF5E  // soft blue-white (#CFE8F0) — general text
#define COLOR_ZIP_TEXT   0x4B2E  // darker slate-teal (#4A6572) — dimmer than COLOR_INFO, used only for the header's ZIP label
#define COLOR_DEL        0xB000  // muted red — DEL key accent, unchanged
// -----------------------------------------------------------

SPIClass tftSPI(HSPI);
Adafruit_ST7796S_kbv tft = Adafruit_ST7796S_kbv(&tftSPI, LCD_DC, LCD_CS, LCD_RST);
// Touch shares this same bus on this board (see the pin note above) — no
// separate SPIClass for touch, unlike the CYD variant.
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
Preferences prefs;

// ---------------- Persisted settings ----------------
String currentZip;
double latitude = 0.0, longitude = 0.0;
bool hasCoords = false;
int havdalahOffsetMin = DEFAULT_HAVDALAH_OFFSET_MIN;
int brightnessPct = DEFAULT_BRIGHTNESS_PCT;
long localUtcOffsetSeconds = 0;
bool hasUtcOffset = false;
String currentSsid = "";
String currentPass = "";
bool hasWifiCreds = false; // false until a real SSID has been saved (see Settings > WI-FI)
bool usingCity = false;      // true if the location came from the CITY list rather than a ZIP
String currentCityLabel = ""; // short "City" name shown in the header when usingCity is true
int cityListTop = 0;          // index of the first visible row in the CITY list screen
bool setupDone = false;       // true once first-run setup (Wi-Fi OR offline SET TIME+CITY OR GPS) has completed once

// GPS mode (persisted). While true, the GPS module is the ONLY source of
// both the time and the location: no Wi-Fi, no NTP, no ZIP/city, no manual
// clock — everything happens automatically, with no user input.
bool gpsMode = false;
// True when the current latitude/longitude came from a GPS fix (persisted),
// so the header keeps showing "GPS 36.17N 115.14W" instead of a stale ZIP
// even if GPS mode is later switched off. Cleared by saveZip()/selectCity().
bool locFromGps = false;
int lastGpsStatusKey = -1; // last status drawn on the "ACQUIRING GPS" screen, so it only redraws on change

// ---------------- First-run setup state (not persisted) ----------------
// Set true only while the SCR_FIRST_RUN sequence is in progress, so the
// SET TIME and Wi-Fi-password flows know to route back into first-run
// (CITY list / SCR_CLOCK) instead of to Settings once they finish.
bool firstRunActive = false;

// ---------------- Runtime state ----------------
time_t candleLightingEpoch = 0;
time_t havdalahEpoch = 0;
bool needsGeocode = false;
unsigned long lastDisplayUpdate = 0;
bool screenNeedsFullRedraw = true;
String lastHeaderTime = ""; // last string drawn by drawHeaderTime(), so it only repaints on change
String lastHeaderLabel = ""; // last location label drawn in the header (ZIP / city / GPS position)

// True once NTP has ever successfully set the system clock this session.
// Nothing that depends on wall-clock time (geocode kickoff, Shabbat-time
// math) runs until this is true, since without it time(nullptr) is
// meaningless. Wi-Fi/NTP is retried in the background from loop() — see
// attemptNtpSync() — so this flips true automatically whenever a
// connection becomes available, with no reboot required.
bool hasWallClock = false;
// True only when the system clock was set via setSystemTimeManually() (the
// fully-offline SET TIME path) rather than from NTP. In that mode
// time(nullptr) holds LOCAL wall-clock values mislabeled as a UTC epoch
// (see setSystemTimeManually()'s comment) — computeShabbatTimes() needs to
// know this so it can line up its true-UTC solar calculation with that
// clock (see solarEpochShiftFor()).
bool clockIsLocalLabeled = false;

// Per-city UTC offset + DST rule, captured from CITY_LIST (cities.h) when a
// city is picked via selectCity(). 255 is the "unset" sentinel — meaning
// either no city has ever been picked, or the location came from a ZIP code
// instead (which has no such table), so solarEpochShiftFor() should fall
// back to the old longitude/hemisphere guess instead of using these fields.
float cityUtcOffsetHours = 0.0f;
uint8_t cityDstRule = 255;

unsigned long lastNtpAttemptMs = 0;
const unsigned long NTP_RETRY_INTERVAL_MS  = 5UL  * 60UL * 1000UL; // retry every 5 min until synced
const unsigned long NTP_RESYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL; // then resync every 6 hrs to correct drift

AppScreen screen = SCR_CLOCK;

KeypadPurpose keypadPurpose = KP_ZIP;
int keypadDigits = 5;
String keypadBuffer = "";
int tmpYear, tmpMonth, tmpDay, tmpHour, tmpMinute; // scratch for the SET TIME sequence

// ---------------- Text entry (Wi-Fi SSID/password keyboard) state ----------------
TextEntryMode textMode = TXT_LOWER;
TextPurpose textPurpose = TXT_SSID;
String textBuffer = "";
int textMaxLen = 32;
String pendingSsid = ""; // holds the SSID between the SSID and password steps

// ---------------- Touch driver: XPT2046 (resistive, shares the display's SPI bus) ----------------
// Raw ADC range the touch controller reports at each axis extreme, now
// mapped onto the FULL 480x320 canvas (see SCR_W/SCR_H above) instead of
// the old 320x240 sub-region. CARRIED OVER UNCHANGED from the CYD sketch's
// values — those were calibrated for a different physical touch digitizer
// on a separate SPI bus, so treat these four numbers as an untested
// starting guess for THIS panel, not a known-good value. TOUCH_DEBUG below
// is already on, so the raw ADC reading is printed in the bottom-left
// corner on every tap — tap the four corners of the screen, note the raw
// numbers, and adjust TS_MINX/MAXX/MINY/MAXY until they bracket what you
// see (min at the top-left-most reading, max at the bottom-right-most).
// Calibrated from all four real corner taps: top-left (3892,3729),
// top-right (296,3818), bottom-right (299,313), bottom-left (3910,250). X
// runs opposite to screen-X (large raw on the LEFT), Y runs opposite to
// screen-Y (large raw at the TOP) — see TOUCH_INVERT_X/Y below. All four
// numbers below now bracket the actual readings from all four corners
// (with a little headroom), not guesses.
#define TS_MINX 280
#define TS_MAXX 3930
#define TS_MINY 230
#define TS_MAXY 3840

// If taps land in a plausible-but-wrong spot after the four numbers above
// are dialed in — e.g. dragging left-to-right moves the cursor up-and-down
// instead — the axes are probably transposed and/or flipped rather than
// just mis-scaled, which four min/max numbers alone can't fix. A
// shared-SPI-bus touch controller (this board) can easily come out with a
// different axis order/direction than a dedicated-bus one (the CYD), and
// this wiring hasn't been verified against real hardware. Try toggling
// these one at a time (reflash after each change) before re-deriving the
// four calibration numbers above:
// Confirmed from the two top-edge taps above: X reads backwards (large raw
// on screen-left) and the top edge's raw Y sits near TS_MAXY, i.e. Y also
// reads backwards. No evidence of a swap (each axis tracked its own screen
// direction, just flipped) — SWAP stays off.
#define TOUCH_SWAP_XY   0   // 1 = swap X and Y before mapping (try this first if axes feel transposed)
#define TOUCH_INVERT_X  1   // 1 = flip left/right
#define TOUCH_INVERT_Y  1   // 1 = flip up/down

// Set to 1 to print raw touch ADC coordinates in the corner of the screen
// on every tap, for recalibrating the constants above. Calibration is now
// done (see the real values above), so this is back to 0 for normal use —
// flip it to 1 again if touch ever needs re-checking on a different unit.
#define TOUCH_DEBUG 0

bool touchInit() {
  // Shares tftSPI with the display (see the pin note near the top of this
  // file) — tftSPI.begin() has already run by the time this is called (see
  // setup()), so this just attaches the touch controller to that same,
  // already-initialized bus rather than starting a second one.
  ts.begin(tftSPI);
  return true;
}

bool getTouchPoint(int16_t &x, int16_t &y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  int32_t rawX = p.x, rawY = p.y;

#if TOUCH_DEBUG
  tft.fillRect(0, SCR_H - 9, 160, 9, COLOR_BG);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_INFO, COLOR_BG);
  tft.setCursor(0, SCR_H - 8);
  tft.print(rawX); tft.print(","); tft.print(rawY);
#endif

#if TOUCH_SWAP_XY
  int32_t tmp = rawX; rawX = rawY; rawY = tmp;
#endif

  x = map(rawX, TS_MINX, TS_MAXX, 0, SCR_W - 1);
  y = map(rawY, TS_MINY, TS_MAXY, 0, SCR_H - 1);

#if TOUCH_INVERT_X
  x = (SCR_W - 1) - x;
#endif
#if TOUCH_INVERT_Y
  y = (SCR_H - 1) - y;
#endif

  x = constrain(x, 0, SCR_W - 1);
  y = constrain(y, 0, SCR_H - 1);
  return true;
}

bool touchPressed(int16_t &x, int16_t &y) {
  static bool wasDown = false;
  int16_t tx, ty;
  bool down = getTouchPoint(tx, ty);
  bool freshPress = down && !wasDown;
  wasDown = down;
  if (freshPress) { x = tx; y = ty; }
  return freshPress;
}

// ---------------- WiFi / NTP ----------------
// Tries to join Wi-Fi and sync the clock via NTP, bounded by timeouts so it
// NEVER blocks forever — if no network is in range, or it joins but NTP
// doesn't answer, this just gives up and returns false. Called once (with
// on-screen feedback) at boot, and re-tried silently in the background from
// loop() so that whenever an internet connection does show up, the clock
// picks up a real time automatically, without needing a reboot.
bool attemptNtpSync(unsigned long wifiTimeoutMs, unsigned long ntpTimeoutMs, bool showUI) {
  if (!hasWifiCreds) return false; // nothing saved yet — see Settings > WI-FI
  int dotsY = 0;
  if (showUI) {
    tft.fillScreen(COLOR_BG);
    drawCandleIcon(CX - 12, 100);
    drawCandleIcon(CX + 8, 100);
    printCentered("CONNECTING TO WI-FI", CX, 130, 2, COLOR_ACCENT_DIM, COLOR_BG);
    tft.setFont(&FreeSansBold12pt7b);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACCENT_DIM, COLOR_BG);
    int16_t dbx, dby; uint16_t dbw, dbh;
    tft.getTextBounds(".", 0, 0, &dbx, &dby, &dbw, &dbh);
    dotsY = 160 - dby; // convert desired top-of-text position to baseline
    tft.setCursor(10, dotsY);
  }

  WiFi.begin(currentSsid.c_str(), currentPass.c_str());
  unsigned long wifiStart = millis();
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStart > wifiTimeoutMs) {
      WiFi.disconnect(true); // give up cleanly; try again later
      if (showUI) tft.setFont();
      return false;
    }
    delay(300);
    if (showUI) {
      tft.print(".");
      if (++dots > 25) { tft.setCursor(10, dotsY); dots = 0; }
    }
  }
  if (showUI) tft.setFont(); // reset to built-in font

  configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // UTC; we do epoch math below
  struct tm timeinfo;
  bool synced = getLocalTime(&timeinfo, ntpTimeoutMs); // getLocalTime() has its own bounded poll
  if (!synced) WiFi.disconnect(true);
  return synced;
}

// ---------------- Portable UTC date/time <-> epoch (no timegm dependency) ----------------
// Howard Hinnant's days_from_civil / civil_from_days algorithms: pure
// integer arithmetic, work on any toolchain without relying on timegm().
static long daysFromCivil(int y, int m, int d) {
  y -= (m <= 2) ? 1 : 0;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void civilFromDays(long z, int &y, int &m, int &d) {
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned long doe = (unsigned long)(z - era * 146097);
  unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long yy = (long)yoe + era * 400;
  unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned long mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp + (mp < 10 ? 3 : -9));
  y = (int)(yy + (m <= 2 ? 1 : 0));
}

static time_t utcToEpoch(int y, int mo, int d, int h, int mi, int se) {
  long days = daysFromCivil(y, mo, d);
  return (time_t)days * 86400L + h * 3600L + mi * 60L + se;
}

static int dayOfYear(int y, int m, int d) {
  static const int cum[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  int doy = cum[m - 1] + d;
  bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  if (leap && m > 2) doy++;
  return doy;
}

// ---------------- NOAA-style solar sunset calculation ----------------
// Standard "Sunrise/Sunset Algorithm" (Nautical Almanac formula, the same
// one behind NOAA's public solar calculator). Returns the UTC time of
// sunset, as fractional hours (0-24), for a given date/location.
// Accurate to within a minute or two — plenty for this purpose.
static double calcSunsetUTCHours(int year, int month, int day, double lat, double lon) {
  int N = dayOfYear(year, month, day);
  double lngHour = lon / 15.0;
  double t = N + ((18.0 - lngHour) / 24.0);

  double M = (0.9856 * t) - 3.289;
  double Mrad = M * DEG_TO_RAD;
  double L = M + (1.916 * sin(Mrad)) + (0.020 * sin(2 * Mrad)) + 282.634;
  L = fmod(L, 360.0); if (L < 0) L += 360.0;
  double Lrad = L * DEG_TO_RAD;

  double RA = RAD_TO_DEG * atan(0.91764 * tan(Lrad));
  RA = fmod(RA, 360.0); if (RA < 0) RA += 360.0;
  double Lquadrant = floor(L / 90.0) * 90.0;
  double RAquadrant = floor(RA / 90.0) * 90.0;
  RA = RA + (Lquadrant - RAquadrant);
  RA = RA / 15.0;

  double sinDec = 0.39782 * sin(Lrad);
  double cosDec = cos(asin(sinDec));
  double zenith = 90.833; // official sunset: 90 deg + refraction + solar radius
  double cosH = (cos(zenith * DEG_TO_RAD) - (sinDec * sin(lat * DEG_TO_RAD)))
                / (cosDec * cos(lat * DEG_TO_RAD));
  if (cosH > 1) cosH = 1;   // sun never sets at this latitude/date (extreme north)
  if (cosH < -1) cosH = -1; // sun never rises

  double H = RAD_TO_DEG * acos(cosH);
  H = H / 15.0;

  double T = H + RA - (0.06571 * t) - 6.622;
  double UT = fmod(T - lngHour, 24.0);
  if (UT < 0) UT += 24.0;
  return UT;
}

static time_t sunsetEpochFor(int year, int month, int day, double lat, double lon) {
  double utcHours = calcSunsetUTCHours(year, month, day, lat, lon);
  return utcToEpoch(year, month, day, 0, 0, 0) + (time_t)lround(utcHours * 3600.0);
}

// Rough, STANDARD-time (no DST) UTC offset estimate from longitude alone
// (15 degrees per hour) — used as a last-resort fallback by
// solarEpochShiftFor() below when no per-city table entry is available.
static long roughUtcOffsetFromLongitude(double lon) {
  return (long)lround(lon / 15.0) * 3600;
}

// Returns the day-of-month of the Nth Sunday of the given month/year (n=1
// for the first Sunday, 2 for the second, etc).
static int nthSundayOfMonth(int year, int month, int n) {
  int count = 0;
  for (int day = 1; day <= 31; day++) {
    long days = daysFromCivil(year, month, day);
    int w = (int)((days + 4) % 7); // Sunday=0 .. Saturday=6, same formula as computeShabbatTimes()
    if (w == 0) {
      count++;
      if (count == n) return day;
    }
  }
  return 1; // unreachable for a real calendar month
}

// Days in the given month (Gregorian, with leap-year-aware February). Used
// by lastSundayOfMonth() below.
static int daysInMonth(int year, int month) {
  static const int DAYS[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month == 2) {
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
  }
  return DAYS[month - 1];
}

// Returns the day-of-month of the LAST Sunday of the given month/year —
// the transition rule used by the EU and (with a two-day offset) Israel.
static int lastSundayOfMonth(int year, int month) {
  int lastDay = daysInMonth(year, month);
  for (int day = lastDay; day >= lastDay - 6; day--) {
    long days = daysFromCivil(year, month, day);
    int w = (int)((days + 4) % 7); // Sunday=0 .. Saturday=6
    if (w == 0) return day;
  }
  return lastDay; // unreachable
}

// Very rough daylight-saving guess, used ONLY as a fallback for a
// ZIP-code-based location (no per-city DST-table entry — see
// solarEpochShiftFor() below). Assumes the common US/Canada-style window —
// DST from the 2nd Sunday in March to the 1st Sunday in November — for the
// Northern Hemisphere, and the mirror-image window (around October to
// April) for the Southern Hemisphere. This is NOT correct everywhere, but
// it's a large accuracy improvement over assuming standard time year-round.
static bool guessDstActive(int year, int month, int day, double lat) {
  long days = daysFromCivil(year, month, day);
  if (lat >= 0) {
    long startDays = daysFromCivil(year, 3, nthSundayOfMonth(year, 3, 2));
    long endDays = daysFromCivil(year, 11, nthSundayOfMonth(year, 11, 1));
    return (days >= startDays) && (days < endDays);
  } else {
    long startDays = daysFromCivil(year, 10, nthSundayOfMonth(year, 10, 1));
    long endDays = daysFromCivil(year, 4, nthSundayOfMonth(year, 4, 1));
    return (days >= startDays) || (days < endDays); // window wraps the new year
  }
}

// Is DST active on the given date, under the given CITY_DST_* rule (see
// cities.h)? The exact, per-city replacement for guessDstActive() above,
// used whenever a city was picked from CITY_LIST (cityDstRule != 255).
static bool dstRuleActive(uint8_t rule, int year, int month, int day) {
  long days = daysFromCivil(year, month, day);
  switch (rule) {
    case CITY_DST_US: {
      long startDays = daysFromCivil(year, 3, nthSundayOfMonth(year, 3, 2));
      long endDays = daysFromCivil(year, 11, nthSundayOfMonth(year, 11, 1));
      return (days >= startDays) && (days < endDays);
    }
    case CITY_DST_EU: {
      long startDays = daysFromCivil(year, 3, lastSundayOfMonth(year, 3));
      long endDays = daysFromCivil(year, 10, lastSundayOfMonth(year, 10));
      return (days >= startDays) && (days < endDays);
    }
    case CITY_DST_AU: {
      long startDays = daysFromCivil(year, 10, nthSundayOfMonth(year, 10, 1));
      long endDays = daysFromCivil(year, 4, nthSundayOfMonth(year, 4, 1));
      return (days >= startDays) || (days < endDays);
    }
    case CITY_DST_NZ: {
      long startDays = daysFromCivil(year, 9, lastSundayOfMonth(year, 9));
      long endDays = daysFromCivil(year, 4, nthSundayOfMonth(year, 4, 1));
      return (days >= startDays) || (days < endDays);
    }
    case CITY_DST_IL: {
      long startDays = daysFromCivil(year, 3, lastSundayOfMonth(year, 3)) - 2; // Friday before
      long endDays = daysFromCivil(year, 10, lastSundayOfMonth(year, 10));
      return (days >= startDays) && (days < endDays);
    }
    case CITY_DST_NONE:
    default:
      return false;
  }
}

// The epoch shift needed to convert sunsetEpochFor()'s true-UTC result for
// the given date into the same "local-labeled" epoch space as the
// fully-offline manually-set clock (see clockIsLocalLabeled and
// setSystemTimeManually()). Zero when the clock is genuinely UTC (NTP path).
// Prefers the exact per-city table data (cityUtcOffsetHours/cityDstRule,
// captured in selectCity() from cities.h); falls back to the old
// longitude/hemisphere guess only when no city table entry is available
// (cityDstRule == 255 — e.g. a ZIP-code-based location).
static long solarEpochShiftFor(int year, int month, int day) {
  if (!clockIsLocalLabeled) return 0;
  if (cityDstRule != 255) {
    long shift = (long)lround(cityUtcOffsetHours * 3600.0);
    if (dstRuleActive(cityDstRule, year, month, day)) shift += 3600;
    return shift;
  }
  long shift = roughUtcOffsetFromLongitude(longitude);
  if (guessDstActive(year, month, day, latitude)) shift += 3600;
  return shift;
}

// ---------------- Compute this/next Shabbat's candle-lighting + havdalah ----------------
// Always finds the most recent Friday (today, if today IS Friday), checks
// whether we're still inside that Shabbat window, and rolls forward a full
// week if it's already passed. Pure local computation — no network needed.
void computeShabbatTimes() {
  time_t now = time(nullptr);
  long daysSinceEpoch = now / 86400;
  int weekday = (int)((daysSinceEpoch + 4) % 7);      // 0=Sun..6=Sat (epoch day 0 was Thursday)
  int daysSinceFriday = (weekday - 5 + 7) % 7;         // 0 if today is Friday, 1 if Sat, etc.
  long fridayDays = daysSinceEpoch - daysSinceFriday;

  for (int attempt = 0; attempt < 2; attempt++) {
    int fy, fm, fd;
    civilFromDays(fridayDays, fy, fm, fd);
    time_t fridaySunset = sunsetEpochFor(fy, fm, fd, latitude, longitude) + solarEpochShiftFor(fy, fm, fd);

    int sy, sm, sd;
    civilFromDays(fridayDays + 1, sy, sm, sd);
    time_t satSunset = sunsetEpochFor(sy, sm, sd, latitude, longitude) + solarEpochShiftFor(sy, sm, sd);

    time_t candidateCandle = fridaySunset - (time_t)CANDLE_LIGHTING_MINUTES * 60;
    time_t candidateHavdalah = satSunset + (time_t)havdalahOffsetMin * 60;

    if (now >= candidateHavdalah) {
      fridayDays += 7; // this Shabbat has fully passed — move to next week
      continue;
    }
    candleLightingEpoch = candidateCandle;
    havdalahEpoch = candidateHavdalah;
    break;
  }
  screenNeedsFullRedraw = true;
}

// ---------------- ZIP -> lat/lon (one-time network lookup, free, no key) ----------------
bool geocodeZip(const String &zip) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.begin("http://api.zippopotam.us/us/" + zip);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, payload)) return false;
  JsonArray places = doc["places"].as<JsonArray>();
  if (places.size() == 0) return false;

  const char* latStr = places[0]["latitude"];
  const char* lonStr = places[0]["longitude"];
  if (!latStr || !lonStr) return false;
  latitude = atof(latStr);
  longitude = atof(lonStr);
  return true;
}

// ---------------- IP-based timezone lookup (Wi-Fi only, no lat/lon needed) ----------------
// Once Wi-Fi is connected we already have a public IP, and that alone is
// enough to look up the real timezone (DST included) — no ZIP/city/lat/lon
// needed at all. This is why it's called right after every successful NTP
// sync, even before a location has been picked. It replaces relying on the
// crude longitude/15 estimate below for anyone who ever connects to Wi-Fi:
// that estimate ignores DST and real timezone/political boundaries, which
// rarely line up with 15-degree longitude bands. If this lookup fails
// (network hiccup, DNS issue, unexpected response), hasUtcOffset is simply
// left false and the longitude fallback still kicks in as before.
bool fetchTimezoneFromIP() {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.begin("http://worldtimeapi.org/api/ip");
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, payload)) return false;
  if (!doc.containsKey("raw_offset") || !doc.containsKey("dst_offset")) return false;

  long rawOffset = doc["raw_offset"].as<long>();
  long dstOffset = doc["dst_offset"].as<long>();
  localUtcOffsetSeconds = rawOffset + dstOffset;
  saveUtcOffset();
  return true;
}

// Called whenever the user explicitly picks a ZIP or CITY (from doGeocode()
// and selectCity() below). Unlike fetchTimezoneFromIP() — which is just an
// initial best-guess based on the device's own network location — this
// ALWAYS overrides the current offset, because the user just told us the
// real location they want times for, which may differ from wherever the
// device's Wi-Fi network physically is.
bool fetchTimezoneForCoords(double lat, double lon) {
  if (WiFi.status() != WL_CONNECTED) return false;
  String url = "https://timeapi.io/api/timezone/coordinate?latitude=" + String(lat, 6) + "&longitude=" + String(lon, 6);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, payload)) return false;
  if (!doc.containsKey("currentUtcOffset") || !doc["currentUtcOffset"].containsKey("seconds")) return false;

  localUtcOffsetSeconds = doc["currentUtcOffset"]["seconds"].as<long>();
  saveUtcOffset();
  return true;
}

void doGeocode() {
  tft.fillRect(0, 30, SCR_W, SCR_H - 30, COLOR_BG);
  printCentered("LOCATING...", CX, 105, 2, COLOR_ACCENT_DIM, COLOR_BG);
  if (geocodeZip(currentZip)) {
    saveCoords(latitude, longitude);
    needsGeocode = false;
    if (hasWallClock) {
      // Only hit the network here when we're actually online — the
      // fully-offline SET TIME + CITY path already has the correct local
      // time from setSystemTimeManually() (offset 0); touching the offset
      // with no network to check against would corrupt an already-correct
      // clock, and picking a location offline only matters for solar math.
      if (WiFi.status() == WL_CONNECTED) {
        if (!fetchTimezoneForCoords(latitude, longitude)) {
          localUtcOffsetSeconds = (long)lround(longitude / 15.0) * 3600; // rough fallback if the lookup fails
          saveUtcOffset();
        }
      }
      computeShabbatTimes();
    }
    // If we don't have a wall clock yet, leave it here — loop()'s
    // background NTP retry will call computeShabbatTimes() itself the
    // moment the clock syncs, now that hasCoords is true.
  } else {
    printCentered("ZIP LOOKUP FAILED", CX, 100, 2, 0xF800, COLOR_BG);
    printCentered("Check ZIP in Settings & retry", CX, 132, 1, COLOR_ACCENT_DIM, COLOR_BG);
    delay(4000);
    needsGeocode = false; // stop retry-looping; user must re-enter ZIP
  }
  screenNeedsFullRedraw = true;
}

// ---------------- Local-time display formatting ----------------
String formatLocalTime(time_t utcEpoch) {
  time_t localEpoch = utcEpoch + localUtcOffsetSeconds;
  long secsOfDay = localEpoch % 86400;
  if (secsOfDay < 0) secsOfDay += 86400;
  int hh = secsOfDay / 3600;
  int mm = (secsOfDay % 3600) / 60;
  const char* ampm = (hh >= 12) ? "PM" : "AM";
  int h12 = hh % 12; if (h12 == 0) h12 = 12;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d %s", h12, mm, ampm);
  return String(buf);
}

// ---------------- Persistence ----------------
void loadSettings() {
  prefs.begin("shabbat", true);
  currentZip = prefs.getString("zip", DEFAULT_ZIP);
  latitude = prefs.getDouble("lat", 0.0);
  longitude = prefs.getDouble("lon", 0.0);
  hasCoords = prefs.getBool("hasCoords", false);
  havdalahOffsetMin = prefs.getInt("havOff", DEFAULT_HAVDALAH_OFFSET_MIN);
  brightnessPct = prefs.getInt("bright", DEFAULT_BRIGHTNESS_PCT);
  localUtcOffsetSeconds = prefs.getLong("utcOff", 0);
  hasUtcOffset = prefs.getBool("hasUtcOff", false);
  currentSsid = prefs.getString("ssid", WIFI_SSID);
  currentPass = prefs.getString("pass", WIFI_PASS);
  hasWifiCreds = currentSsid.length() > 0 && currentSsid != "YOUR_WIFI_SSID";
  usingCity = prefs.getBool("usingCity", false);
  currentCityLabel = prefs.getString("cityLbl", "");
  cityUtcOffsetHours = prefs.getFloat("cityUtcOff", 0.0f);
  cityDstRule = (uint8_t)prefs.getUInt("cityDst", 255);
  setupDone = prefs.getBool("setupDone", false);
  gpsMode = prefs.getBool("gpsMode", false);
  locFromGps = prefs.getBool("locGps", false);
  prefs.end();
}

void markSetupDone() {
  setupDone = true;
  prefs.begin("shabbat", false);
  prefs.putBool("setupDone", true);
  prefs.end();
}

void saveZip(const String &zip) {
  prefs.begin("shabbat", false);
  prefs.putString("zip", zip);
  prefs.putBool("usingCity", false); // switch the header back to ZIP display
  // A ZIP code has no per-city DST-table entry, so clear any leftover city
  // data from a previous selectCity() call — otherwise solarEpochShiftFor()
  // would keep using a stale, unrelated city's offset/DST rule.
  prefs.putFloat("cityUtcOff", 0.0f);
  prefs.putUInt("cityDst", 255);
  prefs.putBool("locGps", false); // the location is about to come from a ZIP, not GPS
  prefs.end();
  currentZip = zip;
  usingCity = false;
  locFromGps = false;
  cityUtcOffsetHours = 0.0f;
  cityDstRule = 255;
}

void saveCoords(double lat, double lon) {
  prefs.begin("shabbat", false);
  prefs.putDouble("lat", lat);
  prefs.putDouble("lon", lon);
  prefs.putBool("hasCoords", true);
  prefs.end();
  hasCoords = true;
}

// Picks a location from the built-in CITY_LIST (cities.h) instead of a US
// ZIP code — the ZIP->lat/lon lookup (api.zippopotam.us) only covers US
// ZIP codes, so this is the path for everyone else. No network lookup is
// needed at all, since the list already carries lat/lon.
void selectCity(int idx) {
  if (idx < 0 || idx >= CITY_COUNT) return;
  if (gpsMode) setGpsMode(false); // picking a city by hand means leaving GPS mode
  latitude = CITY_LIST[idx].lat;
  longitude = CITY_LIST[idx].lon;

  String full = CITY_LIST[idx].name;
  int comma = full.indexOf(',');
  currentCityLabel = (comma >= 0) ? full.substring(0, comma) : full;
  currentCityLabel.toUpperCase();
  usingCity = true;
  locFromGps = false;
  cityUtcOffsetHours = CITY_LIST[idx].utcOffsetHours;
  cityDstRule = CITY_LIST[idx].dstRule;

  prefs.begin("shabbat", false);
  prefs.putDouble("lat", latitude);
  prefs.putDouble("lon", longitude);
  prefs.putBool("hasCoords", true);
  prefs.putBool("usingCity", true);
  prefs.putBool("locGps", false);
  prefs.putString("cityLbl", currentCityLabel);
  prefs.putFloat("cityUtcOff", cityUtcOffsetHours);
  prefs.putUInt("cityDst", (uint32_t)cityDstRule);
  prefs.end();

  hasCoords = true;
  needsGeocode = false; // no ZIP lookup needed — we already have lat/lon
  candleLightingEpoch = 0;
  havdalahEpoch = 0;
  if (hasWallClock) {
    // Always re-derive the timezone for this newly-picked location when
    // we're actually online — it may be a different timezone than
    // wherever the device's own Wi-Fi network is, so the earlier
    // IP-based guess (or a previous ZIP/city's offset) must not be left
    // in place. But if we're offline (the fully-offline SET TIME + CITY
    // first-run path), the clock already holds the correct local time
    // from setSystemTimeManually() (offset 0) — leave it alone, since
    // there's no network to check a real timezone against anyway, and
    // the longitude fallback would only corrupt an already-correct clock.
    if (WiFi.status() == WL_CONNECTED) {
      if (!fetchTimezoneForCoords(latitude, longitude)) {
        localUtcOffsetSeconds = (long)lround(longitude / 15.0) * 3600; // rough fallback if the lookup fails
        saveUtcOffset();
      }
    }
    computeShabbatTimes();
  }
  if (firstRunActive) {
    markSetupDone();
    firstRunActive = false;
  }
  screenNeedsFullRedraw = true;
}

void saveHavdalahOffset() {
  prefs.begin("shabbat", false);
  prefs.putInt("havOff", havdalahOffsetMin);
  prefs.end();
}

void saveBrightness() {
  prefs.begin("shabbat", false);
  prefs.putInt("bright", brightnessPct);
  prefs.end();
}

void saveUtcOffset() {
  prefs.begin("shabbat", false);
  prefs.putLong("utcOff", localUtcOffsetSeconds);
  prefs.putBool("hasUtcOff", true);
  prefs.end();
  hasUtcOffset = true;
}

// Sets the system clock directly from user-entered LOCAL time, for the
// fully offline SET TIME path — there's no NTP/Wi-Fi reference available to
// derive a UTC offset from. localUtcOffsetSeconds is kept at 0 and the
// system clock itself is made to hold local time instead, so downstream
// code (formatLocalTime(), computeShabbatTimes()) — which only needs a
// self-consistent epoch+offset pair — works correctly without ever having
// had a real UTC reference.
void setSystemTimeManually(int year, int month, int day, int hour, int minute) {
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = 0;
  t.tm_isdst = 0;
  time_t epoch = mktime(&t);
  struct timeval tv = { epoch, 0 };
  settimeofday(&tv, nullptr);
}

void saveWifiCreds(const String &ssid, const String &pass) {
  prefs.begin("shabbat", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  currentSsid = ssid;
  currentPass = pass;
  hasWifiCreds = currentSsid.length() > 0 && currentSsid != "YOUR_WIFI_SSID";
  if (gpsMode) {
    // GPS owns the clock and location while it's on. Just remember the
    // credentials (used again if GPS mode is turned off); don't touch the
    // clock — a failed attemptNtpSync() below would clobber a good GPS clock.
    screenNeedsFullRedraw = true;
    return;
  }
  // Try connecting right away instead of waiting for the next background
  // retry, so entering credentials gives immediate feedback.
  hasWallClock = attemptNtpSync(15000, 8000, true);
  clockIsLocalLabeled = false; // NTP-synced clock is true UTC
  lastNtpAttemptMs = millis();
  if (hasWallClock) {
    fetchTimezoneFromIP();
    if (!hasCoords) needsGeocode = true;
    else if (!hasUtcOffset) {
      localUtcOffsetSeconds = (long)lround(longitude / 15.0) * 3600;
      saveUtcOffset();
    }
  }
  screenNeedsFullRedraw = true;
}

void saveZipAndTriggerGeocode(const String &zip) {
  if (gpsMode) setGpsMode(false); // entering a ZIP by hand means leaving GPS mode
  saveZip(zip);
  needsGeocode = true;
  candleLightingEpoch = 0;
  havdalahEpoch = 0;
}

// Parses the compile-time __DATE__ macro ("Mon DD YYYY"). Used by the GPS
// week-rollover guard (the sketch cannot predate its own build date).
void compileBuildDate(int &y, int &mo, int &d) {
  static const char* months[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
  char monStr[4] = {0};
  int day = 1, year = 2026;
  y = 2026; mo = 1; d = 1;
  if (sscanf(__DATE__, "%3s %d %d", monStr, &day, &year) == 3) {
    for (int i = 0; i < 12; i++) {
      if (strncmp(monStr, months[i], 3) == 0) { mo = i + 1; break; }
    }
    d = day;
    y = year;
  }
}

// =========================================================================
// GPS MODE — GY-GPS6MV2 (u-blox NEO-6M) on UART1 (I2C connector pins IO25/IO32) as the time + location source
//
// While gpsMode is on, the GPS module is the ONLY source of the clock and the
// location. The first valid fix sets the system clock to true UTC (the same
// kind of clock NTP gives, so clockIsLocalLabeled stays false and the existing
// solar math is used unchanged), adopts the fix's latitude/longitude, and
// picks a UTC offset + DST rule from the nearest entry in CITY_LIST. After
// that it keeps the clock trimmed every ~10 minutes and follows the device if
// it moves more than ~5 km. Nothing needs a button press, a Wi-Fi network or
// a ZIP/city. Time zone note: the GPS fix gives an exact LOCATION but no time
// zone, so the zone comes from the nearest listed city — that can be wrong
// near a time-zone border far from any listed city. (The candle-lighting and
// havdalah INSTANTS and the countdown are computed from lat/lon in true UTC,
// so they stay correct either way; only the wall-clock labels would be off.)
// =========================================================================

// ---- GPS PARSER BEGIN (plain C++ — this section is also compiled and unit-tested on a PC) ----
static char gpsLine[100];
static uint8_t gpsLineLen = 0;
static bool gpsInSentence = false;
unsigned long gpsCharsSeen = 0;    // every byte received from the module
unsigned long gpsSentencesOk = 0;  // sentences that passed their NMEA checksum
int gpsSatsTracked = 0;            // satellites with a real signal (SNR > 0), from the latest complete GSV cycle
static int gpsGsvRunning = 0;

void gpsOnFix(double lat, double lon, int y, int mo, int d, int h, int mi, int s); // defined below the parser

static bool gpsTwoDigits(const char* p, int &out) {
  if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return false;
  out = (p[0] - '0') * 10 + (p[1] - '0');
  return true;
}

// NMEA coordinates are ddmm.mmmm (latitude) or dddmm.mmmm (longitude);
// dividing by 100 and splitting off the whole part handles both.
static double gpsNmeaToDegrees(const char* s) {
  double v = atof(s);
  double deg = floor(v / 100.0);
  return deg + (v - deg * 100.0) / 60.0;
}

// s points just after the '$'. Verifies the "*HH" checksum (XOR of every
// character between '$' and '*') and, if good, cuts the string at the '*'.
static bool gpsChecksumOk(char* s) {
  char* star = strchr(s, '*');
  if (!star || !isxdigit((unsigned char)star[1]) || !isxdigit((unsigned char)star[2])) return false;
  uint8_t cs = 0;
  for (char* p = s; p < star; p++) cs ^= (uint8_t)*p;
  char hex[3] = { star[1], star[2], 0 };
  uint8_t want = (uint8_t)strtoul(hex, nullptr, 16);
  *star = 0;
  return cs == want;
}

// Splits s in place on commas; f[] receives pointers to each field (empty
// fields come back as ""). Returns the field count.
static int gpsSplit(char* s, char** f, int maxf) {
  int n = 0;
  f[n++] = s;
  for (char* p = s; *p && n < maxf; p++) {
    if (*p == ',') { *p = 0; f[n++] = p + 1; }
  }
  return n;
}

// $--RMC,hhmmss.ss,A,ddmm.mmmm,N,dddmm.mmmm,E,speed,course,ddmmyy,...
// Status 'A' = valid fix, 'V' = warning (no fix) — 'V' sentences are ignored
// entirely, including their time/date, since an unlocked receiver can emit a
// default/guessed date.
static void gpsHandleRMC(char** f, int nf) {
  if (nf < 10 || f[2][0] != 'A') return;
  int hh, mi, ss, dd, mo, yy;
  if (strlen(f[1]) < 6 || !gpsTwoDigits(f[1], hh) || !gpsTwoDigits(f[1] + 2, mi) || !gpsTwoDigits(f[1] + 4, ss)) return;
  if (strlen(f[9]) < 6 || !gpsTwoDigits(f[9], dd) || !gpsTwoDigits(f[9] + 2, mo) || !gpsTwoDigits(f[9] + 4, yy)) return;
  if (hh > 23 || mi > 59 || ss > 60 || dd < 1 || dd > 31 || mo < 1 || mo > 12) return;
  if (ss == 60) ss = 59; // leap second
  if (f[3][0] == 0 || f[5][0] == 0) return;
  if ((f[4][0] != 'N' && f[4][0] != 'S') || (f[6][0] != 'E' && f[6][0] != 'W')) return;
  double lat = gpsNmeaToDegrees(f[3]); if (f[4][0] == 'S') lat = -lat;
  double lon = gpsNmeaToDegrees(f[5]); if (f[6][0] == 'W') lon = -lon;
  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return;
  if (lat == 0.0 && lon == 0.0) return; // "null island" is what an unparsed/empty fix looks like
  gpsOnFix(lat, lon, 2000 + yy, mo, dd, hh, mi, ss);
}

// $--GSV,totalMsgs,msgNum,satsInView,{PRN,elev,azimuth,SNR} x up to 4
// An empty/zero SNR means the satellite is known (from the almanac) but not
// actually being received — so counting SNR > 0 is a real "is the antenna
// hearing anything" signal, unlike the in-view count.
static void gpsHandleGSV(char** f, int nf) {
  if (nf < 4) return;
  int total = atoi(f[1]), num = atoi(f[2]);
  if (num == 1) gpsGsvRunning = 0;
  for (int i = 4; i + 3 < nf; i += 4) {
    if (f[i + 3][0] != 0 && atoi(f[i + 3]) > 0) gpsGsvRunning++;
  }
  if (num == total) gpsSatsTracked = gpsGsvRunning;
}

static void gpsHandleSentence(char* s) {
  if (!gpsChecksumOk(s)) return;
  gpsSentencesOk++;
  char* f[24];
  int nf = gpsSplit(s, f, 24);
  if (strlen(f[0]) != 5) return; // "GPRMC", "GNGSV", ... — 2-letter talker + 3-letter type
  const char* type = f[0] + 2;
  if (strcmp(type, "RMC") == 0) gpsHandleRMC(f, nf);
  else if (strcmp(type, "GSV") == 0) gpsHandleGSV(f, nf);
}

void gpsFeedByte(char c) {
  gpsCharsSeen++;
  if (c == '$') { gpsLineLen = 0; gpsInSentence = true; return; }
  if (!gpsInSentence) return;
  if (c == '\r' || c == '\n') {
    if (gpsLineLen > 0) { gpsLine[gpsLineLen] = 0; gpsHandleSentence(gpsLine); }
    gpsInSentence = false;
    gpsLineLen = 0;
    return;
  }
  if (gpsLineLen < sizeof(gpsLine) - 1) gpsLine[gpsLineLen++] = c;
  else { gpsInSentence = false; gpsLineLen = 0; } // overlong line = garbage; drop it
}
// ---- GPS PARSER END ----

// ---- GPS session state ----
unsigned long gpsStartMs = 0;
bool gpsSerialStarted = false;
bool gpsClockSetOnce = false;      // has this GPS session set the system clock yet?
unsigned long gpsLastClockSetMs = 0;
bool gpsPosApplied = false;        // has this GPS session adopted a position yet?
double gpsAppliedLat = 0.0, gpsAppliedLon = 0.0;
unsigned long gpsLastPersistMs = 0; // 0 = not persisted yet this session
unsigned long gpsLastLogMs = 0;
const unsigned long GPS_CLOCK_RESYNC_MS = 10UL * 60UL * 1000UL;     // re-trim the clock this often
const unsigned long GPS_PERSIST_MIN_INTERVAL_MS = 10UL * 60UL * 1000UL; // flash-wear limit on saving the position
const unsigned long GPS_ACQUIRE_REFRESH_MS = 30UL * 1000UL;         // screen refresh rate while waiting for the first fix
const double GPS_MOVE_THRESHOLD_DEG = 0.05;                         // ~5.5 km — re-adopt the position past this

void gpsResetSession() {
  gpsClockSetOnce = false;
  gpsLastClockSetMs = 0;
  gpsPosApplied = false;
  gpsLastPersistMs = 0;
}

void gpsBegin() {
  if (gpsSerialStarted) return;
  Serial1.setRxBufferSize(1024); // must precede begin(); holds ~1 s of NMEA if the main loop is briefly busy (e.g. a Wi-Fi call)
  // UART1 on the CrowPanel header: RX = IO18 (GPS TX wire), TX = IO17 (GPS RX wire).
  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  gpsSerialStarted = true;
  gpsStartMs = millis();
  gpsCharsSeen = 0;
  gpsSentencesOk = 0;
  gpsSatsTracked = 0;
  Serial.printf("[gps] UART1 started: ESP RX=IO%d (GPS TX), ESP TX=IO%d (GPS RX), %d baud\n", GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
}

void gpsEnd() {
  if (!gpsSerialStarted) return;
  Serial1.end();
  gpsSerialStarted = false;
}

// Called every loop() iteration (any screen). Non-blocking; capped so a
// flood of bytes can never starve the button/UI handling.
void gpsPoll() {
  if (!gpsMode || !gpsSerialStarted) return;
  int budget = 512;
  while (budget-- > 0 && Serial1.available() > 0) gpsFeedByte((char)Serial1.read());
  if (millis() - gpsLastLogMs > 15000) {
    gpsLastLogMs = millis();
    Serial.printf("[gps] bytes=%lu sentences=%lu tracking=%d clockSet=%d\n",
                  gpsCharsSeen, gpsSentencesOk, gpsSatsTracked, (int)gpsClockSetOnce);
  }
}

// Index of the CITY_LIST entry closest to (lat, lon). Squared planar
// distance with longitude scaled by cos(lat) — plenty accurate for "which
// listed city is nearest"; no trig-heavy great-circle math needed.
static int gpsNearestCityIndex(double lat, double lon) {
  int best = 0;
  double bestD = 1e18;
  double cl = cos(lat * DEG_TO_RAD);
  for (int i = 0; i < CITY_COUNT; i++) {
    double dy = (double)CITY_LIST[i].lat - lat;
    double dx = (double)CITY_LIST[i].lon - lon;
    if (dx > 180.0) dx -= 360.0;
    if (dx < -180.0) dx += 360.0;
    dx *= cl;
    double d2 = dx * dx + dy * dy;
    if (d2 < bestD) { bestD = d2; best = i; }
  }
  return best;
}

// Recomputes localUtcOffsetSeconds (standard offset + DST if the city's rule
// says it's active today) from cityUtcOffsetHours/cityDstRule. Only valid
// with a true-UTC clock. Cheap, so it's also called once a minute in GPS
// mode — that's what carries the display across a DST changeover.
void applyCityTableOffset() {
  if (cityDstRule == 255) return;
  long stdOff = (long)lround(cityUtcOffsetHours * 3600.0);
  long localDays = (long)((time(nullptr) + stdOff) / 86400);
  int y, m, d;
  civilFromDays(localDays, y, m, d);
  long off = stdOff + (dstRuleActive(cityDstRule, y, m, d) ? 3600 : 0);
  if (!hasUtcOffset || off != localUtcOffsetSeconds) {
    localUtcOffsetSeconds = off;
    saveUtcOffset();
  }
}

// A valid (status 'A') RMC fix arrived — called by the parser, about once a
// second. Most calls return immediately; real work happens on the first fix,
// every GPS_CLOCK_RESYNC_MS, and when the position has moved noticeably.
void gpsOnFix(double lat, double lon, int y, int mo, int d, int h, int mi, int s) {
  if (!gpsMode) return;
  // After a slow screen refresh the UART buffer holds a backlog of OLD
  // sentences; their timestamps are seconds stale. Only act on a sentence
  // that was processed (nearly) live, i.e. with little left queued behind it.
  if (Serial1.available() > 120) return;

  unsigned long nowMs = millis();
  bool wasMissingClock = !hasWallClock;
  bool clockDue = !gpsClockSetOnce || !hasWallClock || clockIsLocalLabeled
                  || (nowMs - gpsLastClockSetMs > GPS_CLOCK_RESYNC_MS);
  bool posDue = !gpsPosApplied
                || fabs(lat - gpsAppliedLat) > GPS_MOVE_THRESHOLD_DEG
                || fabs(lon - gpsAppliedLon) > GPS_MOVE_THRESHOLD_DEG;
  if (!clockDue && !posDue) return;

  if (clockDue) {
    time_t t = utcToEpoch(y, mo, d, h, mi, s);
    // GPS week-number rollover guard: some u-blox 6 firmware reports dates
    // exactly 1024 weeks (7168 days) in the past, i.e. ~20 years early. The
    // sketch can't have been built before its own compile date, so any GPS
    // date earlier than that is wrong by whole rollovers; add them back
    // (time-of-day and weekday are unaffected by a rollover).
    int by, bm, bd;
    compileBuildDate(by, bm, bd);
    time_t floorT = utcToEpoch(by, bm, bd, 0, 0, 0) - 86400;
    for (int i = 0; i < 3 && t < floorT; i++) t += (time_t)7168 * 86400;
    struct timeval tv = { t, 0 };
    settimeofday(&tv, nullptr);
    gpsLastClockSetMs = nowMs;
    gpsClockSetOnce = true;
    hasWallClock = true;
    clockIsLocalLabeled = false; // GPS time is true UTC, same as NTP
    Serial.printf("[gps] clock set from GPS: %04d-%02d-%02d %02d:%02d:%02d UTC\n", y, mo, d, h, mi, s);
  }

  if (posDue) {
    latitude = lat;
    longitude = lon;
    hasCoords = true;
    gpsAppliedLat = lat;
    gpsAppliedLon = lon;
    gpsPosApplied = true;
    locFromGps = true;
    usingCity = false;
    needsGeocode = false;
    int ci = gpsNearestCityIndex(lat, lon);
    cityUtcOffsetHours = CITY_LIST[ci].utcOffsetHours;
    cityDstRule = CITY_LIST[ci].dstRule;
    Serial.printf("[gps] position %.4f, %.4f -> time zone from nearest listed city: %s\n", lat, lon, CITY_LIST[ci].name);
    // Persist (rate-limited — a moving device would otherwise rewrite flash
    // every few seconds). Only a "last known location" for the header; the
    // clock itself can't survive a reboot without a new fix anyway.
    if (gpsLastPersistMs == 0 || nowMs - gpsLastPersistMs > GPS_PERSIST_MIN_INTERVAL_MS) {
      prefs.begin("shabbat", false);
      prefs.putDouble("lat", lat);
      prefs.putDouble("lon", lon);
      prefs.putBool("hasCoords", true);
      prefs.putBool("usingCity", false);
      prefs.putBool("locGps", true);
      prefs.putFloat("cityUtcOff", cityUtcOffsetHours);
      prefs.putUInt("cityDst", (uint32_t)cityDstRule);
      prefs.end();
      gpsLastPersistMs = nowMs;
    }
  }

  applyCityTableOffset();
  // computeShabbatTimes() flags a full screen refresh, so only call it when
  // something it depends on really changed — not on every routine clock trim.
  if (posDue || wasMissingClock) computeShabbatTimes();
}

// Turns GPS mode on/off and persists the choice. Turning it OFF leaves the
// current clock and location in place and (if Wi-Fi credentials exist) lets
// the normal background NTP sync take over again right away.
void setGpsMode(bool on) {
  gpsMode = on;
  prefs.begin("shabbat", false);
  prefs.putBool("gpsMode", on);
  prefs.end();
  if (on) {
    gpsResetSession();
    gpsBegin();
  } else {
    gpsEnd();
    lastNtpAttemptMs = millis() - NTP_RESYNC_INTERVAL_MS - 1UL; // "NTP is due now"
  }
  screenNeedsFullRedraw = true;
}

// ---------------- Brightness (PWM-dimmable backlight) ----------------
void applyBrightness() {
  int duty = map(brightnessPct, 0, 100, 0, 255);
  ledcWrite(BL_PWM_CHANNEL, duty);
}

// ---------------- Chrome helpers (header/captions/menus only) ----------------
// Old call sites pass "size" 1-4, same as before — it now selects one of
// four fixed-point-size fonts instead of scaling the blocky built-in font.
const GFXfont* chromeFont(uint8_t size) {
  switch (size) {
    case 1:  return &FreeSansBold9pt7b;
    case 2:  return &FreeSansBold12pt7b;
    case 3:  return &FreeSansBold18pt7b;
    default: return &FreeSansBold24pt7b;
  }
}

// Custom GFX fonts position text by their BASELINE, not top-left corner
// like the built-in font, so centering has to account for getTextBounds()'
// x1/y1 offsets (the gap between the cursor and the glyphs' actual ink) —
// otherwise text lands off-target both horizontally and vertically.
void printCentered(const char* txt, int cx, int y, uint8_t size, uint16_t fg, uint16_t bg) {
  tft.setFont(chromeFont(size));
  tft.setTextSize(1); // fonts are already the right point size; no scaling
  tft.setTextColor(fg, bg);
  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds(txt, 0, 0, &bx, &by, &bw, &bh);
  tft.setCursor(cx - bw / 2 - bx, y - by);
  tft.print(txt);
  tft.setFont(); // reset to the built-in font so unrelated prints elsewhere
                 // (e.g. the clock's D/HH/MM/SS column labels) aren't affected
}

// Small candle-and-flame icon, drawn from primitives — no image assets needed.
void drawCandleIcon(int x, int y) {
  tft.fillRect(x, y + 6, 6, 14, COLOR_INFO);       // wax body
  tft.fillRect(x - 1, y + 4, 8, 2, COLOR_ACCENT);  // rim
  tft.fillTriangle(x + 3, y - 6, x, y + 4, x + 6, y + 4, COLOR_ACCENT); // flame
  tft.fillTriangle(x + 3, y - 2, x + 1, y + 4, x + 5, y + 4, 0xFEA0);   // flame core
}

// Small wrench icon (settings button), drawn from primitives.
// cx/cy is the icon's center; the wrench is drawn on a diagonal (handle
// lower-left to head upper-right), sized loosely off r so it still fits
// the same touch target as the old gear icon did.
void drawGearIcon(int cx, int cy, int r) {
  float ang = -45.0f * DEG_TO_RAD; // handle-to-head direction
  float ux = cos(ang), uy = sin(ang);
  float px = -uy, py = ux; // perpendicular, for handle thickness

  int handleLen = (int)(r * 1.9f);
  int headR = (int)(r * 0.62f);
  int jawR = (int)(r * 0.34f);
  float halfW = r * 0.24f;

  // Handle: a thick line from the tail up to just short of the head.
  int tailX = cx - (int)(ux * handleLen);
  int tailY = cy - (int)(uy * handleLen);
  int neckX = cx + (int)(ux * headR * 0.3f);
  int neckY = cy + (int)(uy * headR * 0.3f);
  int x0 = tailX + (int)(px * halfW), y0 = tailY + (int)(py * halfW);
  int x1 = tailX - (int)(px * halfW), y1 = tailY - (int)(py * halfW);
  int x2 = neckX - (int)(px * halfW), y2 = neckY - (int)(py * halfW);
  int x3 = neckX + (int)(px * halfW), y3 = neckY + (int)(py * halfW);
  tft.fillTriangle(x0, y0, x1, y1, x2, y2, COLOR_ACCENT);
  tft.fillTriangle(x0, y0, x2, y2, x3, y3, COLOR_ACCENT);
  tft.fillCircle(tailX, tailY, (int)halfW, COLOR_ACCENT); // rounded butt end

  // Head: an open-jaw ring at the far (upper-right) end.
  int headCx = cx + (int)(ux * headR * 0.55f);
  int headCy = cy + (int)(uy * headR * 0.55f);
  tft.fillCircle(headCx, headCy, headR, COLOR_ACCENT);
  tft.fillCircle(headCx, headCy, headR - 3, COLOR_PANEL);
  // Punch the jaw opening out of the ring, facing further up-right.
  int jawCx = headCx + (int)(ux * headR * 0.9f);
  int jawCy = headCy + (int)(uy * headR * 0.9f);
  tft.fillCircle(jawCx, jawCy, jawR, COLOR_PANEL);
}

// =========================================================================
// Seven-segment digit rendering
// =========================================================================
// Segment bits: a=top, b=top-right, c=bottom-right, d=bottom,
//               e=bottom-left, f=top-left, g=middle
const uint8_t SEG_MAP[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

// Draws one digit (0-9) as seven segments inside a w x h box at (x,y).
// Unlit segments are drawn dim, lit ones bright (the "ghost" LED look).
void drawSevenSegDigit(int x, int y, int w, int h, int digit) {
  uint8_t segs = (digit >= 0 && digit <= 9) ? SEG_MAP[digit] : 0x00;
  int t = w / 4;
  int vSegH = (h - 3 * t) / 2;
  int midY = y + t + vSegH;

  auto seg = [&](bool on) { return on ? COLOR_SEG_ON : COLOR_SEG_OFF; };

  tft.fillRect(x + t, y, w - 2 * t, t, seg(segs & 0x01));                 // a
  tft.fillRect(x + w - t, y + t, t, vSegH, seg(segs & 0x02));             // b
  tft.fillRect(x + w - t, midY + t, t, vSegH, seg(segs & 0x04));          // c
  tft.fillRect(x + t, y + h - t, w - 2 * t, t, seg(segs & 0x08));         // d
  tft.fillRect(x, midY + t, t, vSegH, seg(segs & 0x10));                  // e
  tft.fillRect(x, y + t, t, vSegH, seg(segs & 0x20));                     // f
  tft.fillRect(x + t, midY, w - 2 * t, t, seg(segs & 0x40));              // g
}

void drawColon(int x, int y, int h) {
  int size = 6;
  tft.fillRect(x, y + h / 3 - size / 2, size, size, COLOR_SEG_ON);
  tft.fillRect(x, y + 2 * h / 3 - size / 2, size, size, COLOR_SEG_ON);
}

// ---------- Clock layout: D : HH : MM : SS, fixed width, centered ----------
// Bigger than the CYD/ES3C28P's digits (30x54) since this panel's 480-wide
// canvas has the room for it — sized so the whole 7-digit group plus
// colons fills most of the 480px width with even margins either side.
#define DIGIT_W 44
#define DIGIT_H 80
#define COLON_W 20
#define SEG_GAP 8
#define CLOCK_Y 95

int digitX[7];
int colonX[3];
int lastDigits[7] = { -1, -1, -1, -1, -1, -1, -1 };

void computeClockLayout() {
  int totalW = 7 * DIGIT_W + 3 * COLON_W + 9 * SEG_GAP;
  int startX = (SCR_W - totalW) / 2;
  int x = startX;

  digitX[0] = x; x += DIGIT_W + SEG_GAP;
  colonX[0] = x; x += COLON_W + SEG_GAP;
  digitX[1] = x; x += DIGIT_W + SEG_GAP;
  digitX[2] = x; x += DIGIT_W + SEG_GAP;
  colonX[1] = x; x += COLON_W + SEG_GAP;
  digitX[3] = x; x += DIGIT_W + SEG_GAP;
  digitX[4] = x; x += DIGIT_W + SEG_GAP;
  colonX[2] = x; x += COLON_W + SEG_GAP;
  digitX[5] = x; x += DIGIT_W + SEG_GAP;
  digitX[6] = x;
}

void drawColons() {
  for (int i = 0; i < 3; i++) drawColon(colonX[i], CLOCK_Y, DIGIT_H);
}

void updateClockDigits(int d, int h1, int h2, int m1, int m2, int s1, int s2, bool forceAll) {
  int newDigits[7] = { d, h1, h2, m1, m2, s1, s2 };
  for (int i = 0; i < 7; i++) {
    if (forceAll || newDigits[i] != lastDigits[i]) {
      drawSevenSegDigit(digitX[i], CLOCK_Y, DIGIT_W, DIGIT_H, newDigits[i]);
      lastDigits[i] = newDigits[i];
    }
  }
}

// =========================================================================
// Screen drawing: main clock
// =========================================================================
// Location label shown at the left of the header: the ZIP or city when
// picked by hand, the GPS position (e.g. "GPS 36.17N 115.14W") when it came
// from a GPS fix, or "GPS SEARCHING" before the first fix.
String headerLabelText() {
  if (locFromGps && hasCoords) {
    char gb[32];
    snprintf(gb, sizeof(gb), "GPS %.2f%c %.2f%c",
             fabs(latitude), latitude >= 0 ? 'N' : 'S',
             fabs(longitude), longitude >= 0 ? 'E' : 'W');
    return String(gb);
  }
  if (gpsMode && !hasCoords) return String("GPS SEARCHING");
  return usingCity ? currentCityLabel : ("ZIP " + currentZip);
}

// Repaints just the label region (left of the clock readout), and only if
// the text changed. Cheap to call every second; this is how the header picks
// up a GPS fix without a full screen redraw.
void drawHeaderLabel() {
  String zipText = headerLabelText();
  if (zipText == lastHeaderLabel) return;
  lastHeaderLabel = zipText;
  tft.fillRect(30, 1, HDR_TIME_LEFT - 30, 28, COLOR_PANEL);
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_ZIP_TEXT, COLOR_PANEL);
  int16_t zbx, zby; uint16_t zbw, zbh;
  tft.getTextBounds(zipText.c_str(), 0, 0, &zbx, &zby, &zbw, &zbh);
  tft.setCursor(34 - zbx, 9 - zby); // top-left at (34,9), vertically balanced with the candle icons
  tft.print(zipText);
  tft.setFont(); // reset to built-in font
}

void drawStaticUI() {
  tft.fillScreen(COLOR_BG);

  tft.fillRect(0, 0, SCR_W, 30, COLOR_PANEL);
  tft.drawFastHLine(0, 30, SCR_W, COLOR_ACCENT);

  drawCandleIcon(8, 14);
  drawCandleIcon(18, 14);
  lastHeaderLabel = "\x01"; // sentinel: force the location label to repaint
  drawHeaderLabel();

  // Wrench settings button, top-right (no visible box; the tap target in
  // loop() is still the same GEAR_HIT_* region the box used to outline)
  drawGearIcon(GEAR_X, GEAR_Y, 7);

  for (int i = 0; i < 7; i++) lastDigits[i] = -1;
  screenNeedsFullRedraw = true;
  lastHeaderTime = "\x01"; // sentinel: force drawHeaderTime() to redraw on next call
  drawHeaderTime();
}

// Live clock-of-day readout in the header, between the ZIP label and the
// wrench icon. Only drawn once we actually have a synced wall clock; kept
// blank until then rather than showing a meaningless time. Cheap to call
// every second — it only repaints its small region, and only when the
// displayed string actually changes (once a minute), so there's no flicker.
void drawHeaderTime() {
  String t = hasWallClock ? formatLocalTime(time(nullptr)) : "";
  if (t == lastHeaderTime) return;
  lastHeaderTime = t;

  tft.fillRect(HDR_TIME_LEFT, 1, HDR_TIME_RIGHT - HDR_TIME_LEFT, 28, COLOR_PANEL); // clear previous text, stay clear of the wrench hit target
  if (t.length() == 0) return;

  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_ZIP_TEXT, COLOR_PANEL);
  int16_t tbx, tby; uint16_t tbw, tbh;
  tft.getTextBounds(t.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);
  tft.setCursor(HDR_TIME_RIGHT - tbw - tbx, 9 - tby); // right-aligned, ending just left of the wrench
  tft.print(t);
  tft.setFont();
}

// Draws a D/HH/MM/SS caption centered under a digit group, using the actual
// measured text width rather than a fixed offset, so "D" (one digit wide)
// and "HH"/"MM"/"SS" (two digits + the gap between them wide) all land
// centered under their own group regardless of how many characters they are.
void drawDigitGroupLabel(const char* label, int groupX, int groupW, int y) {
  tft.setTextSize(1);
  tft.setTextColor(COLOR_ACCENT_DIM, COLOR_BG);
  int16_t lbx, lby; uint16_t lbw, lbh;
  tft.getTextBounds(label, 0, 0, &lbx, &lby, &lbw, &lbh);
  tft.setCursor(groupX + (groupW - (int)lbw) / 2 - lbx, y);
  tft.print(label);
}

// Shown in the countdown area while GPS mode is waiting for its first fix.
// The wording doubles as a bring-up diagnostic, since "no wire", "wrong baud"
// and "no sky view" are the three ways a GPS module typically fails to work.
// State 0 = acquiring, 1 = no bytes at all, 2 = bytes but no valid sentences.
int gpsAcquireState() {
  unsigned long up = millis() - gpsStartMs;
  if (gpsCharsSeen == 0 && up > 8000) return 1;
  if (gpsCharsSeen > 0 && gpsSentencesOk == 0 && up > 10000) return 2;
  return 0;
}

// Combined state + satellite count, compared each second so the acquiring
// screen is only redrawn when something on it would actually change.
int gpsStatusKey() {
  int sats = gpsSatsTracked > 99 ? 99 : gpsSatsTracked;
  return gpsAcquireState() * 100 + sats;
}

void drawGpsAcquiring() {
  char buf[48];
  int st = gpsAcquireState();
  if (st == 1) {
    printCentered("NO DATA FROM GPS", CX, 80, 2, COLOR_ACCENT, COLOR_BG);
    snprintf(buf, sizeof(buf), "check wiring: GPS TX -> IO%d", GPS_RX_PIN);
    printCentered(buf, CX, 115, 1, COLOR_INFO, COLOR_BG);
    printCentered("and GPS VCC -> 3V3, GND -> GND", CX, 135, 1, COLOR_INFO, COLOR_BG);
  } else if (st == 2) {
    printCentered("GPS DATA UNREADABLE", CX, 80, 2, COLOR_ACCENT, COLOR_BG);
    snprintf(buf, sizeof(buf), "expected %d baud - check module", GPS_BAUD);
    printCentered(buf, CX, 115, 1, COLOR_INFO, COLOR_BG);
  } else {
    printCentered("ACQUIRING GPS...", CX, 80, 2, COLOR_ACCENT_DIM, COLOR_BG);
    snprintf(buf, sizeof(buf), "tracking %d satellite%s", gpsSatsTracked, gpsSatsTracked == 1 ? "" : "s");
    printCentered(buf, CX, 115, 1, COLOR_INFO, COLOR_BG);
    printCentered("needs open sky - try a window", CX, 135, 1, COLOR_INFO, COLOR_BG);
  }
}

void drawCountdownFrame() {
  tft.fillRect(0, 30, SCR_W, SCR_H - 30, COLOR_BG);

  // Unified gating: !hasWallClock covers both "never synced" (online path)
  // AND "never manually set" (offline path) in one check, since either one
  // leaves the clock unusable. The message branches on hasWifiCreds so an
  // offline (manually-set clock, no Wi-Fi) device doesn't wrongly show a
  // Wi-Fi nag once its clock has been set.
  if (!hasWallClock) {
    if (gpsMode) {
      drawGpsAcquiring();
    } else if (hasWifiCreds) {
      printCentered("SYNCING TIME...", CX, 95, 2, COLOR_ACCENT_DIM, COLOR_BG);
      printCentered("(waiting on Wi-Fi/NTP)", CX, 125, 1, COLOR_ACCENT_DIM, COLOR_BG);
    } else {
      printCentered("SET UP WI-FI OR SET TIME", CX, 95, 2, COLOR_ACCENT_DIM, COLOR_BG);
      printCentered("(tap the wrench - Settings)", CX, 125, 1, COLOR_ACCENT_DIM, COLOR_BG);
    }
    return;
  }
  if (!hasCoords) {
    if (gpsMode) {
      printCentered("WAITING FOR GPS FIX", CX, 95, 2, COLOR_ACCENT_DIM, COLOR_BG);
      printCentered("(needs open sky)", CX, 125, 1, COLOR_ACCENT_DIM, COLOR_BG);
      return;
    }
    printCentered("NO LOCATION SET", CX, 95, 2, COLOR_ACCENT_DIM, COLOR_BG);
    printCentered("(tap the wrench - Settings)", CX, 125, 1, COLOR_ACCENT_DIM, COLOR_BG);
    return;
  }
  if (needsGeocode) {
    printCentered("LOCATING...", CX, 105, 2, COLOR_ACCENT_DIM, COLOR_BG);
    return;
  }
  if (candleLightingEpoch == 0 || havdalahEpoch == 0) {
    printCentered("CALCULATING...", CX, 105, 2, COLOR_ACCENT_DIM, COLOR_BG);
    return;
  }

  printCentered("TIME REMAINING", CX, 40, 3, COLOR_ACCENT, COLOR_BG);

  int fx0 = digitX[0] - 14, fy0 = CLOCK_Y - 12;
  int fx1 = digitX[6] + DIGIT_W + 14, fy1 = CLOCK_Y + DIGIT_H + 22;
  tft.drawRoundRect(fx0, fy0, fx1 - fx0, fy1 - fy0, 8, COLOR_PANEL_EDGE);

  String candleLine = "CANDLES " + formatLocalTime(candleLightingEpoch);
  String havdalahLine = "HAVDALAH " + formatLocalTime(havdalahEpoch);

  // Two lines, center-justified, vertically centered together as one block
  // in the remaining space below the clock frame down to the bottom of the
  // content area — rather than each line centered in its own half, which
  // could visually skew the pair off-center as a group.
  int areaTop = fy1 + 8;       // just under the frame
  int areaBottom = SCR_H - 20; // near the bottom of the drawable content region
  int areaHeight = areaBottom - areaTop;

  tft.setFont(chromeFont(2));
  tft.setTextSize(1);
  int16_t mbx, mby; uint16_t mbw, mbh;
  tft.getTextBounds(candleLine.c_str(), 0, 0, &mbx, &mby, &mbw, &mbh);
  tft.setFont();

  int lineH = (int)mbh;
  int lineGap = 8;
  int blockH = lineH * 2 + lineGap;
  int blockTop = areaTop + (areaHeight - blockH) / 2;
  int line1Y = blockTop;
  int line2Y = blockTop + lineH + lineGap;

  printCentered(candleLine.c_str(), CX, line1Y, 2, COLOR_INFO, COLOR_BG);
  printCentered(havdalahLine.c_str(), CX, line2Y, 2, COLOR_INFO, COLOR_BG);

  // D / HH / MM / SS labels, each centered under its digit group rather than
  // left-anchored at a fixed offset.
  int hhGroupW = DIGIT_W * 2 + SEG_GAP;
  drawDigitGroupLabel("D",  digitX[0], DIGIT_W,   CLOCK_Y + DIGIT_H + 8);
  drawDigitGroupLabel("HH", digitX[1], hhGroupW, CLOCK_Y + DIGIT_H + 8);
  drawDigitGroupLabel("MM", digitX[3], hhGroupW, CLOCK_Y + DIGIT_H + 8);
  drawDigitGroupLabel("SS", digitX[5], hhGroupW, CLOCK_Y + DIGIT_H + 8);

  drawColons();
  updateClockDigits(0, 0, 0, 0, 0, 0, 0, true);
}

// Runs every second. Counts down to candle-lighting until it passes, then
// automatically switches target to havdalah. Once havdalah passes, rolls
// to next week's Shabbat (cheap, purely local — no network needed).
void updateCountdown() {
  if (needsGeocode || candleLightingEpoch == 0 || havdalahEpoch == 0) return;

  time_t now = time(nullptr);
  if (now >= havdalahEpoch) {
    computeShabbatTimes();
    return;
  }

  time_t target = (now < candleLightingEpoch) ? candleLightingEpoch : havdalahEpoch;
  long secsLeft = (long)(target - now);
  if (secsLeft < 0) secsLeft = 0;

  int days = secsLeft / 86400;
  int hours = (secsLeft % 86400) / 3600;
  int mins = (secsLeft % 3600) / 60;
  int secs = secsLeft % 60;

  updateClockDigits(days % 10, hours / 10, hours % 10,
                     mins / 10, mins % 10, secs / 10, secs % 10, false);
}

// =========================================================================
// Screen: first-run setup choice — shown once, before setupDone is true.
// Offers the existing Wi-Fi flow (auto time via NTP; location is set
// separately in Settings, or defaults to Las Vegas) or a fully offline
// path (manual SET TIME + pick-a-city, no network ever required).
// =========================================================================
void drawFirstRunScreen() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, SCR_W, 26, COLOR_PANEL);
  tft.drawFastHLine(0, 26, SCR_W, COLOR_ACCENT);
  printCentered("WELCOME - SET UP CLOCK", CX, 5, 1, COLOR_INFO, COLOR_PANEL);

  tft.fillRoundRect(40, 38, 400, 78, 10, COLOR_PANEL);
  tft.drawRoundRect(40, 38, 400, 78, 10, COLOR_PANEL_EDGE);
  printCentered("CONNECT TO WI-FI", CX, 55, 2, COLOR_INFO, COLOR_PANEL);
  printCentered("auto time - set location after", CX, 90, 1, COLOR_ACCENT_DIM, COLOR_PANEL);

  tft.fillRoundRect(40, 124, 400, 78, 10, COLOR_PANEL);
  tft.drawRoundRect(40, 124, 400, 78, 10, COLOR_ACCENT);
  printCentered("SET TIME + CITY", CX, 141, 2, COLOR_ACCENT, COLOR_PANEL);
  printCentered("offline, no network needed", CX, 176, 1, COLOR_ACCENT_DIM, COLOR_PANEL);

  tft.fillRoundRect(40, 210, 400, 78, 10, COLOR_PANEL);
  tft.drawRoundRect(40, 210, 400, 78, 10, COLOR_PANEL_EDGE);
  printCentered("GPS MODULE", CX, 227, 2, COLOR_INFO, COLOR_PANEL);
  printCentered("fully automatic - time + place", CX, 262, 1, COLOR_ACCENT_DIM, COLOR_PANEL);
}

void handleFirstRunTouch(int16_t x, int16_t y) {
  if (x < 40 || x > 440) return;
  if (y >= 38 && y <= 116) {
    firstRunActive = true;
    startTextEntry(TXT_SSID, 32, currentSsid);
  } else if (y >= 124 && y <= 202) {
    firstRunActive = true;
    startKeypad(KP_YEAR, 4);
  } else if (y >= 210 && y <= 288) {
    // GPS needs no further input: it supplies the clock and the location by
    // itself, so setup is complete the moment this is chosen.
    firstRunActive = false;
    markSetupDone();
    setGpsMode(true);
    screen = SCR_CLOCK;
    drawStaticUI();
  }
}

// =========================================================================
// Screen drawing: settings menu
// =========================================================================
// =========================================================================
// Screen: CITY list — an alternative to typing a ZIP code, since the ZIP
// lookup only covers the US. Shows 6 rows at a time; the up/down arrows on
// the right page by a full screen, and tapping a row selects that city
// immediately (no separate OK step needed — a tap already is the "select").
// =========================================================================
#define CITY_ROWS_VISIBLE 6
#define CITY_ROW_H 40
#define CITY_ROW_W 380
#define CITY_LIST_TOP 30
#define CITY_ARROW_X 400
#define CITY_ARROW_W 60
#define CITY_BACK_Y 280

void drawCityListScreen() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, SCR_W, 26, COLOR_PANEL);
  tft.drawFastHLine(0, 26, SCR_W, COLOR_ACCENT);
  printCentered("SELECT CITY", CX, 4, 1, COLOR_INFO, COLOR_PANEL);

  for (int i = 0; i < CITY_ROWS_VISIBLE; i++) {
    int idx = cityListTop + i;
    if (idx >= CITY_COUNT) break;
    int y = CITY_LIST_TOP + i * CITY_ROW_H;
    tft.fillRoundRect(10, y, CITY_ROW_W, CITY_ROW_H - 3, 5, COLOR_PANEL);
    tft.drawRoundRect(10, y, CITY_ROW_W, CITY_ROW_H - 3, 5, COLOR_PANEL_EDGE);
    printCentered(CITY_LIST[idx].name, 10 + CITY_ROW_W / 2, y + 10, 1, COLOR_INFO, COLOR_PANEL);
  }

  int arrowH = (CITY_ROWS_VISIBLE * CITY_ROW_H - 4) / 2;
  tft.fillRoundRect(CITY_ARROW_X, CITY_LIST_TOP, CITY_ARROW_W, arrowH, 6, COLOR_PANEL);
  tft.drawRoundRect(CITY_ARROW_X, CITY_LIST_TOP, CITY_ARROW_W, arrowH, 6, COLOR_PANEL_EDGE);
  printCentered("^", CITY_ARROW_X + CITY_ARROW_W / 2, CITY_LIST_TOP + arrowH / 2 - 12, 2, COLOR_INFO, COLOR_PANEL);
  int arrow2Y = CITY_LIST_TOP + arrowH + 4;
  tft.fillRoundRect(CITY_ARROW_X, arrow2Y, CITY_ARROW_W, arrowH, 6, COLOR_PANEL);
  tft.drawRoundRect(CITY_ARROW_X, arrow2Y, CITY_ARROW_W, arrowH, 6, COLOR_PANEL_EDGE);
  printCentered("v", CITY_ARROW_X + CITY_ARROW_W / 2, arrow2Y + arrowH / 2 - 12, 2, COLOR_INFO, COLOR_PANEL);

  tft.fillRoundRect(CX - 80, CITY_BACK_Y, 160, 34, 8, COLOR_ACCENT);
  printCentered("BACK", CX, CITY_BACK_Y + 9, 1, COLOR_BG, COLOR_ACCENT);
}

void handleCityListTouch(int16_t x, int16_t y) {
  int arrowH = (CITY_ROWS_VISIBLE * CITY_ROW_H - 4) / 2;
  int arrow2Y = CITY_LIST_TOP + arrowH + 4;
  if (x >= CITY_ARROW_X && x <= CITY_ARROW_X + CITY_ARROW_W) {
    if (y >= CITY_LIST_TOP && y <= CITY_LIST_TOP + arrowH) {
      cityListTop = max(0, cityListTop - CITY_ROWS_VISIBLE);
      drawCityListScreen();
    } else if (y >= arrow2Y && y <= arrow2Y + arrowH) {
      int maxTop = CITY_COUNT - CITY_ROWS_VISIBLE;
      if (maxTop < 0) maxTop = 0;
      cityListTop = min(maxTop, cityListTop + CITY_ROWS_VISIBLE);
      drawCityListScreen();
    }
    return;
  }
  if (x >= CX - 80 && x <= CX + 80 && y >= CITY_BACK_Y && y <= CITY_BACK_Y + 34) {
    screen = SCR_SETTINGS;
    drawSettingsMenu();
    return;
  }
  if (x >= 10 && x <= 10 + CITY_ROW_W && y >= CITY_LIST_TOP && y < CITY_LIST_TOP + CITY_ROWS_VISIBLE * CITY_ROW_H) {
    int row = (y - CITY_LIST_TOP) / CITY_ROW_H;
    int idx = cityListTop + row;
    if (idx < CITY_COUNT) {
      selectCity(idx);
      screen = SCR_CLOCK;
      drawStaticUI();
    }
  }
}

void drawSettingsMenu() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, SCR_W, 26, COLOR_PANEL);
  tft.drawFastHLine(0, 26, SCR_W, COLOR_ACCENT);
  printCentered("SETTINGS", CX, 5, 1, COLOR_INFO, COLOR_PANEL);

  // ZIP CODE and CITY render as two half-width buttons side by side in one
  // row — a visual cue that they're alternatives, not two separate settings
  // — which frees up vertical space for the remaining rows to breathe.
  // Sized to use the full 480px width and 320px height of this panel.
  const int rowH = 30, rowStep = 36, top = 34, margin = 20;
  const int fullW = SCR_W - 2 * margin;   // 440
  const int halfW = (fullW - 16) / 2;     // 212, with a 16px gap between halves

  tft.fillRoundRect(margin, top, halfW, rowH, 7, COLOR_PANEL);
  tft.drawRoundRect(margin, top, halfW, rowH, 7, COLOR_PANEL_EDGE);
  printCentered("ZIP CODE", margin + halfW / 2, top + 8, 1, COLOR_INFO, COLOR_PANEL);
  int cityX = margin + halfW + 16;
  tft.fillRoundRect(cityX, top, halfW, rowH, 7, COLOR_PANEL);
  tft.drawRoundRect(cityX, top, halfW, rowH, 7, COLOR_PANEL_EDGE);
  printCentered("CITY", cityX + halfW / 2, top + 8, 1, COLOR_INFO, COLOR_PANEL);

  // WI-FI and GPS are likewise two half-width buttons in one row: both are
  // alternative ways of getting the time, so GPS shows its on/off state.
  tft.fillRoundRect(margin, top + rowStep, halfW, rowH, 7, COLOR_PANEL);
  tft.drawRoundRect(margin, top + rowStep, halfW, rowH, 7, COLOR_PANEL_EDGE);
  printCentered("WI-FI", margin + halfW / 2, top + rowStep + 7, 2, COLOR_INFO, COLOR_PANEL);
  tft.fillRoundRect(cityX, top + rowStep, halfW, rowH, 7, COLOR_PANEL);
  tft.drawRoundRect(cityX, top + rowStep, halfW, rowH, 7, gpsMode ? COLOR_ACCENT : COLOR_PANEL_EDGE);
  printCentered(gpsMode ? "GPS: ON" : "GPS: OFF", cityX + halfW / 2, top + rowStep + 7, 2, gpsMode ? COLOR_ACCENT : COLOR_INFO, COLOR_PANEL);

  const char* labels[5] = { "BRIGHTNESS", "SET TIME", "HAVDALAH OFFSET", "SYSTEM RESET", "BACK" };
  for (int i = 0; i < 5; i++) {
    int y = top + (i + 2) * rowStep;
    bool isBack = (i == 4);
    bool isReset = (i == 3);
    tft.fillRoundRect(margin, y, fullW, rowH, 7, COLOR_PANEL);
    tft.drawRoundRect(margin, y, fullW, rowH, 7, isBack ? COLOR_ACCENT : (isReset ? COLOR_DEL : COLOR_PANEL_EDGE));
    printCentered(labels[i], CX, y + 7, 2, isBack ? COLOR_ACCENT : (isReset ? COLOR_DEL : COLOR_INFO), COLOR_PANEL);
  }
}

int settingsMenuHit(int16_t x, int16_t y) {
  const int rowH = 30, rowStep = 36, top = 34, margin = 20;
  const int fullW = SCR_W - 2 * margin;
  const int halfW = (fullW - 16) / 2;
  int cityX = margin + halfW + 16;
  if (y >= top && y <= top + rowH) {
    if (x >= margin && x < margin + halfW) return 0; // ZIP CODE (left half)
    if (x >= cityX && x <= cityX + halfW) return 1;  // CITY (right half)
    return -1;
  }
  if (y >= top + rowStep && y <= top + rowStep + rowH) {
    if (x >= margin && x < margin + halfW) return 2; // WI-FI (left half)
    if (x >= cityX && x <= cityX + halfW) return 3;  // GPS toggle (right half)
    return -1;
  }
  if (x < margin || x > margin + fullW) return -1;
  for (int i = 0; i < 5; i++) {
    int ry = top + (i + 2) * rowStep;
    if (y >= ry && y <= ry + rowH) return i + 4;
  }
  return -1;
}

void startKeypad(KeypadPurpose p, int digits) {
  keypadPurpose = p;
  keypadDigits = digits;
  keypadBuffer = "";
  screen = SCR_KEYPAD;
  drawKeypad();
}

void handleSettingsTouch(int16_t x, int16_t y) {
  int hit = settingsMenuHit(x, y);
  if (hit < 0) return;
  switch (hit) {
    case 0: startKeypad(KP_ZIP, 5); break;
    case 1: cityListTop = 0; screen = SCR_CITY_LIST; drawCityListScreen(); break;
    case 2: startTextEntry(TXT_SSID, 32, currentSsid); break;
    case 3:
      setGpsMode(!gpsMode);
      if (gpsMode) { screen = SCR_CLOCK; drawStaticUI(); } // show the "ACQUIRING GPS" status right away
      else drawSettingsMenu();                             // just repaint the toggle
      break;
    case 4: screen = SCR_BRIGHTNESS; drawBrightnessScreen(); break;
    case 5: startKeypad(KP_YEAR, 4); break;
    case 6: startKeypad(KP_HAVDALAH, 3); break;
    case 7: screen = SCR_CONFIRM_RESET; drawConfirmResetScreen(); break;
    case 8: screen = SCR_CLOCK; drawStaticUI(); break;
  }
}

// =========================================================================
// Screen: SYSTEM RESET confirmation — reachable from Settings. Wipes every
// persisted key (Wi-Fi creds, location, time offset, havdalah offset, etc.)
// and drops the device back into the SCR_FIRST_RUN flow, same as a fresh
// unconfigured boot.
// =========================================================================
void drawConfirmResetScreen() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, SCR_W, 26, COLOR_PANEL);
  tft.drawFastHLine(0, 26, SCR_W, COLOR_ACCENT);
  printCentered("SYSTEM RESET", CX, 5, 1, COLOR_INFO, COLOR_PANEL);

  printCentered("ARE YOU SURE?", CX, 55, 2, COLOR_ACCENT, COLOR_BG);
  printCentered("erases wifi, location, time", CX, 90, 1, COLOR_ACCENT_DIM, COLOR_BG);
  printCentered("and all settings", CX, 106, 1, COLOR_ACCENT_DIM, COLOR_BG);

  tft.fillRoundRect(20, 180, 210, 90, 10, COLOR_PANEL);
  tft.drawRoundRect(20, 180, 210, 90, 10, COLOR_DEL);
  printCentered("YES, RESET", 125, 218, 1, COLOR_DEL, COLOR_PANEL);

  tft.fillRoundRect(250, 180, 210, 90, 10, COLOR_PANEL);
  tft.drawRoundRect(250, 180, 210, 90, 10, COLOR_ACCENT);
  printCentered("CANCEL", 355, 218, 1, COLOR_ACCENT, COLOR_PANEL);
}

// Wipes the entire "shabbat" Preferences namespace, resets in-RAM state to
// just-booted defaults (mirroring what loadSettings() would load from an
// empty namespace), and re-enters SCR_FIRST_RUN — the same state a brand
// new, never-configured device would be in.
void performSystemReset() {
  prefs.begin("shabbat", false);
  prefs.clear();
  prefs.end();

  currentZip = DEFAULT_ZIP;
  latitude = 0.0;
  longitude = 0.0;
  hasCoords = false;
  havdalahOffsetMin = DEFAULT_HAVDALAH_OFFSET_MIN;
  brightnessPct = DEFAULT_BRIGHTNESS_PCT;
  localUtcOffsetSeconds = 0;
  hasUtcOffset = false;
  currentSsid = "";
  currentPass = "";
  hasWifiCreds = false;
  usingCity = false;
  currentCityLabel = "";
  cityUtcOffsetHours = 0.0f;
  cityDstRule = 255;
  setupDone = false;
  gpsMode = false;
  locFromGps = false;
  gpsEnd();
  gpsResetSession();
  candleLightingEpoch = 0;
  havdalahEpoch = 0;
  hasWallClock = false;
  clockIsLocalLabeled = false;
  needsGeocode = false;
  screenNeedsFullRedraw = true;
  applyBrightness();

  firstRunActive = true;
  screen = SCR_FIRST_RUN;
  drawFirstRunScreen();
}

void handleConfirmResetTouch(int16_t x, int16_t y) {
  if (y < 180 || y > 270) return;
  if (x >= 20 && x <= 230) {
    performSystemReset();
  } else if (x >= 250 && x <= 460) {
    screen = SCR_SETTINGS; drawSettingsMenu();
  }
}

// =========================================================================
// Screen drawing: brightness
// =========================================================================
void drawBrightnessScreen() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, SCR_W, 26, COLOR_PANEL);
  tft.drawFastHLine(0, 26, SCR_W, COLOR_ACCENT);
  printCentered("BRIGHTNESS", CX, 5, 1, COLOR_INFO, COLOR_PANEL);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", brightnessPct);
  printCentered(buf, CX, 90, 4, COLOR_ACCENT, COLOR_BG);

  tft.fillRoundRect(60, 190, 110, 70, 10, COLOR_PANEL);
  tft.drawRoundRect(60, 190, 110, 70, 10, COLOR_PANEL_EDGE);
  printCentered("-", 115, 210, 3, COLOR_INFO, COLOR_PANEL);

  tft.fillRoundRect(310, 190, 110, 70, 10, COLOR_PANEL);
  tft.drawRoundRect(310, 190, 110, 70, 10, COLOR_PANEL_EDGE);
  printCentered("+", 365, 210, 3, COLOR_INFO, COLOR_PANEL);

  tft.fillRoundRect(190, 280, 100, 34, 8, COLOR_ACCENT);
  printCentered("DONE", CX, 289, 1, COLOR_BG, COLOR_ACCENT);
}

void handleBrightnessTouch(int16_t x, int16_t y) {
  if (x >= 60 && x <= 170 && y >= 190 && y <= 260) {
    brightnessPct = max(10, brightnessPct - 10);
    applyBrightness(); saveBrightness(); drawBrightnessScreen();
  } else if (x >= 310 && x <= 420 && y >= 190 && y <= 260) {
    brightnessPct = min(100, brightnessPct + 10);
    applyBrightness(); saveBrightness(); drawBrightnessScreen();
  } else if (x >= 190 && x <= 290 && y >= 280 && y <= 314) {
    screen = SCR_SETTINGS; drawSettingsMenu();
  }
}

// =========================================================================
// Screen drawing: generalized numeric keypad (ZIP / havdalah offset / time)
// =========================================================================
const char* keypadTitle() {
  switch (keypadPurpose) {
    case KP_ZIP:      return "ENTER ZIP CODE";
    case KP_HAVDALAH: return "HAVDALAH OFFSET (MIN)";
    case KP_YEAR:     return "ENTER YEAR";
    case KP_MONTH:    return "ENTER MONTH (1-12)";
    case KP_DAY:      return "ENTER DAY (1-31)";
    case KP_HOUR:     return "ENTER HOUR (0-23)";
    case KP_MINUTE:   return "ENTER MINUTE (0-59)";
  }
  return "";
}

#define KP_TOP 82
#define KP_ROW_STEP 52
#define KP_KEY_W 143
#define KP_KEY_GAP 15
#define KP_KEY_H 44

void drawKeypad() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, SCR_W, 26, COLOR_PANEL);
  tft.drawFastHLine(0, 26, SCR_W, COLOR_ACCENT);
  printCentered(keypadTitle(), CX, 5, 1, COLOR_INFO, COLOR_PANEL);

  int n = keypadDigits;
  int boxW = 40, boxGap = 10;
  int startX = CX - (n * boxW + (n - 1) * boxGap) / 2;
  for (int i = 0; i < n; i++) {
    int bx = startX + i * (boxW + boxGap);
    bool filled = i < keypadBuffer.length();
    tft.drawRoundRect(bx, 36, boxW, 34, 5, filled ? COLOR_ACCENT : COLOR_PANEL_EDGE);
    if (filled) {
      char c[2] = { keypadBuffer[i], 0 };
      printCentered(c, bx + boxW / 2, 45, 2, COLOR_INFO, COLOR_BG);
    }
  }

  const char* keys[12] = {"1","2","3","4","5","6","7","8","9","DEL","0","OK"};
  int idx = 0;
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 3; col++) {
      int x = 10 + col * (KP_KEY_W + KP_KEY_GAP);
      int y = KP_TOP + row * KP_ROW_STEP;
      uint16_t keyColor = COLOR_INFO;
      uint16_t borderColor = COLOR_PANEL_EDGE;
      if (keys[idx][0] == 'D') { keyColor = COLOR_DEL; borderColor = COLOR_DEL; }

      if (keys[idx][0] == 'O') {
        tft.fillRoundRect(x, y, KP_KEY_W, KP_KEY_H, 8, COLOR_ACCENT);
        printCentered(keys[idx], x + KP_KEY_W / 2, y + 14, 2, COLOR_BG, COLOR_ACCENT);
      } else {
        tft.fillRoundRect(x, y, KP_KEY_W, KP_KEY_H, 8, COLOR_PANEL);
        tft.drawRoundRect(x, y, KP_KEY_W, KP_KEY_H, 8, borderColor);
        printCentered(keys[idx], x + KP_KEY_W / 2, y + 14, 2, keyColor, COLOR_PANEL);
      }
      idx++;
    }
  }
}

int keypadHit(int16_t x, int16_t y) {
  if (y < KP_TOP || y > KP_TOP + 4 * KP_ROW_STEP) return -1;
  int row = (y - KP_TOP) / KP_ROW_STEP;
  int col = (x - 10) / (KP_KEY_W + KP_KEY_GAP);
  if (row < 0 || row > 3 || col < 0 || col > 2) return -1;
  return row * 3 + col;
}

void handleKeypadTouch(int16_t x, int16_t y) {
  int hit = keypadHit(x, y);
  if (hit < 0) return;
  const char* keys[12] = {"1","2","3","4","5","6","7","8","9","DEL","0","OK"};
  String key = keys[hit];

  if (key == "DEL") {
    if (keypadBuffer.length()) keypadBuffer.remove(keypadBuffer.length() - 1);
    drawKeypad();
    return;
  }
  if (key == "OK") {
    if ((int)keypadBuffer.length() != keypadDigits) return; // incomplete, ignore
    switch (keypadPurpose) {
      case KP_ZIP:
        saveZipAndTriggerGeocode(keypadBuffer);
        screen = SCR_CLOCK; drawStaticUI();
        break;
      case KP_HAVDALAH:
        havdalahOffsetMin = constrain(keypadBuffer.toInt(), 0, 180);
        saveHavdalahOffset();
        if (hasCoords) computeShabbatTimes();
        screen = SCR_SETTINGS; drawSettingsMenu();
        break;
      case KP_YEAR:
        tmpYear = keypadBuffer.toInt();
        startKeypad(KP_MONTH, 2);
        break;
      case KP_MONTH:
        tmpMonth = constrain(keypadBuffer.toInt(), 1, 12);
        startKeypad(KP_DAY, 2);
        break;
      case KP_DAY:
        tmpDay = constrain(keypadBuffer.toInt(), 1, 31);
        startKeypad(KP_HOUR, 2);
        break;
      case KP_HOUR:
        tmpHour = constrain(keypadBuffer.toInt(), 0, 23);
        startKeypad(KP_MINUTE, 2);
        break;
      case KP_MINUTE: {
        tmpMinute = constrain(keypadBuffer.toInt(), 0, 59);
        if (gpsMode) setGpsMode(false); // setting the clock by hand means leaving GPS mode
        if (hasWifiCreds) {
          // Online path: derive the local UTC offset by comparing the
          // entered local time against the NTP-verified system clock.
          time_t enteredLocalEpoch = utcToEpoch(tmpYear, tmpMonth, tmpDay, tmpHour, tmpMinute, 0);
          time_t nowUtc = time(nullptr);
          localUtcOffsetSeconds = (long)(enteredLocalEpoch - nowUtc);
          saveUtcOffset();
          clockIsLocalLabeled = false;
        } else {
          // Offline path: no NTP reference exists at all, so set the
          // system clock directly to the entered LOCAL time and keep the
          // offset at 0. Unlike the online path, this clock holds LOCAL
          // values mislabeled as UTC, so clockIsLocalLabeled is set so
          // computeShabbatTimes()'s solarEpochShiftFor() can correct for it.
          setSystemTimeManually(tmpYear, tmpMonth, tmpDay, tmpHour, tmpMinute);
          localUtcOffsetSeconds = 0;
          saveUtcOffset();
          hasWallClock = true;
          clockIsLocalLabeled = true;
        }
        if (hasCoords) computeShabbatTimes();
        if (firstRunActive) {
          cityListTop = 0; screen = SCR_CITY_LIST; drawCityListScreen();
        } else {
          screen = SCR_SETTINGS; drawSettingsMenu();
        }
        break;
      }
    }
    return;
  }
  // digit key
  if ((int)keypadBuffer.length() < keypadDigits) keypadBuffer += key;
  drawKeypad();
}

// =========================================================================
// Screen drawing: alphanumeric keyboard (Wi-Fi SSID / password entry)
// =========================================================================
// A 3-mode (lowercase/uppercase/symbols) on-screen keyboard, separate from
// the numeric keypad above since Wi-Fi credentials need letters and
// symbols. The symbol set covers the common ones seen in real passwords
// (!@#$%^&*()-_+=.,?) but isn't exhaustive — there's no room on a 320x240
// screen for every printable ASCII character with legible touch targets.
void startTextEntry(TextPurpose p, int maxLen, const String &initial) {
  textPurpose = p;
  textMaxLen = maxLen;
  textBuffer = initial;
  textMode = TXT_LOWER;
  screen = SCR_TEXTPAD;
  drawTextPad();
}

void drawTextField() {
  tft.fillRect(0, 30, SCR_W, 30, COLOR_BG);
  tft.drawRoundRect(4, 32, SCR_W - 8, 26, 5, COLOR_PANEL_EDGE);
  String shown = textBuffer;
  const int maxChars = 44; // roughly what fits at size-1 in the wider field width
  if ((int)shown.length() > maxChars) {
    shown = shown.substring(shown.length() - maxChars); // scroll to show the tail
  }
  shown += "_"; // simple cursor
  tft.setTextSize(1);
  tft.setTextColor(COLOR_INFO, COLOR_BG);
  tft.setCursor(10, 40);
  tft.print(shown);
}

const char* textRow0() { return (textMode == TXT_SYMBOLS) ? "1234567890" : (textMode == TXT_UPPER ? "QWERTYUIOP" : "qwertyuiop"); }
const char* textRow1() { return (textMode == TXT_SYMBOLS) ? "!@#$%^&*(" : (textMode == TXT_UPPER ? "ASDFGHJKL" : "asdfghjkl"); }
const char* textRow2() { return (textMode == TXT_SYMBOLS) ? ")_+=.,?"   : (textMode == TXT_UPPER ? "ZXCVBNM"   : "zxcvbnm"); }

// Text keyboard layout: wider keys (480px canvas) and taller rows/keys
// (320px canvas) than the CYD's original 320x240 keyboard.
#define TXT_KB_TOP 70
#define TXT_KB_ROW_STEP 52
#define TXT_KB_KEY_H 46
#define TXT_KB_W0 (SCR_W / 10)     // row 0: 10 keys, full width
#define TXT_KB_W1 (SCR_W / 9)      // row 1: 9 keys, full width
#define TXT_KB_W2 48               // row 2: middle keys
#define TXT_KB_MODE_X 1
#define TXT_KB_MODE_W 69
#define TXT_KB_ROW2_X 72
#define TXT_KB_DEL_X 410
#define TXT_KB_DEL_W 69
#define TXT_KB_SPACE_X 1
#define TXT_KB_SPACE_W 372
#define TXT_KB_OK_X 375
#define TXT_KB_OK_W 104

void drawTextPad() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, SCR_W, 26, COLOR_PANEL);
  tft.drawFastHLine(0, 26, SCR_W, COLOR_ACCENT);
  printCentered(textPurpose == TXT_SSID ? "WI-FI NETWORK NAME" : "WI-FI PASSWORD",
                CX, 5, 1, COLOR_INFO, COLOR_PANEL);

  drawTextField();

  const int kbTop = TXT_KB_TOP, rowStep = TXT_KB_ROW_STEP, keyH = TXT_KB_KEY_H;
  const char* row0 = textRow0();
  const char* row1 = textRow1();
  const char* row2 = textRow2();

  // Row 0: 10 keys, full width
  int w0 = TXT_KB_W0;
  for (int i = 0; i < 10; i++) {
    char c[2] = { row0[i], 0 };
    int x = i * w0;
    tft.fillRoundRect(x + 1, kbTop, w0 - 2, keyH, 4, COLOR_PANEL);
    tft.drawRoundRect(x + 1, kbTop, w0 - 2, keyH, 4, COLOR_PANEL_EDGE);
    printCentered(c, x + w0 / 2, kbTop + 12, 2, COLOR_INFO, COLOR_PANEL);
  }

  // Row 1: 9 keys
  int y1 = kbTop + rowStep;
  int w1 = TXT_KB_W1;
  for (int i = 0; i < 9; i++) {
    char c[2] = { row1[i], 0 };
    int x = i * w1;
    tft.fillRoundRect(x + 1, y1, w1 - 2, keyH, 4, COLOR_PANEL);
    tft.drawRoundRect(x + 1, y1, w1 - 2, keyH, 4, COLOR_PANEL_EDGE);
    printCentered(c, x + w1 / 2, y1 + 12, 2, COLOR_INFO, COLOR_PANEL);
  }

  // Row 2: MODE key + 7 keys + DEL
  int y2 = kbTop + 2 * rowStep;
  const char* modeLabel = (textMode == TXT_LOWER) ? "ABC" : (textMode == TXT_UPPER ? "123" : "abc");
  tft.fillRoundRect(TXT_KB_MODE_X, y2, TXT_KB_MODE_W, keyH, 4, COLOR_PANEL);
  tft.drawRoundRect(TXT_KB_MODE_X, y2, TXT_KB_MODE_W, keyH, 4, COLOR_ACCENT);
  printCentered(modeLabel, TXT_KB_MODE_X + TXT_KB_MODE_W / 2, y2 + 12, 1, COLOR_ACCENT, COLOR_PANEL);

  int w2 = TXT_KB_W2;
  for (int i = 0; i < 7; i++) {
    char c[2] = { row2[i], 0 };
    int x = TXT_KB_ROW2_X + i * w2;
    tft.fillRoundRect(x + 1, y2, w2 - 2, keyH, 4, COLOR_PANEL);
    tft.drawRoundRect(x + 1, y2, w2 - 2, keyH, 4, COLOR_PANEL_EDGE);
    printCentered(c, x + w2 / 2, y2 + 12, 2, COLOR_INFO, COLOR_PANEL);
  }

  tft.fillRoundRect(TXT_KB_DEL_X, y2, TXT_KB_DEL_W, keyH, 4, COLOR_PANEL);
  tft.drawRoundRect(TXT_KB_DEL_X, y2, TXT_KB_DEL_W, keyH, 4, COLOR_DEL);
  printCentered("DEL", TXT_KB_DEL_X + TXT_KB_DEL_W / 2, y2 + 12, 1, COLOR_DEL, COLOR_PANEL);

  // Row 3: SPACE + OK
  int y3 = kbTop + 3 * rowStep;
  tft.fillRoundRect(TXT_KB_SPACE_X, y3, TXT_KB_SPACE_W, keyH, 4, COLOR_PANEL);
  tft.drawRoundRect(TXT_KB_SPACE_X, y3, TXT_KB_SPACE_W, keyH, 4, COLOR_PANEL_EDGE);
  printCentered("SPACE", TXT_KB_SPACE_X + TXT_KB_SPACE_W / 2, y3 + 12, 1, COLOR_INFO, COLOR_PANEL);

  tft.fillRoundRect(TXT_KB_OK_X, y3, TXT_KB_OK_W, keyH, 4, COLOR_ACCENT);
  printCentered("OK", TXT_KB_OK_X + TXT_KB_OK_W / 2, y3 + 12, 1, COLOR_BG, COLOR_ACCENT);
}

// Returns a key code: 0-9 (row 0), 100-108 (row 1), 200-206 (row 2 middle),
// 300 (MODE), 301 (DEL), 302 (SPACE), 303 (OK), or -1 for no hit.
int textPadHit(int16_t x, int16_t y) {
  const int kbTop = TXT_KB_TOP, rowStep = TXT_KB_ROW_STEP;
  if (y < kbTop || y > kbTop + 4 * rowStep) return -1;
  int row = (y - kbTop) / rowStep;

  if (row == 0) {
    int col = x / TXT_KB_W0;
    if (col < 0 || col > 9) return -1;
    return col;
  }
  if (row == 1) {
    int col = x / TXT_KB_W1;
    if (col < 0 || col > 8) return -1;
    return 100 + col;
  }
  if (row == 2) {
    if (x < TXT_KB_ROW2_X) return 300;
    if (x >= TXT_KB_DEL_X) return 301;
    int col = (x - TXT_KB_ROW2_X) / TXT_KB_W2;
    if (col < 0 || col > 6) return -1;
    return 200 + col;
  }
  if (row == 3) {
    return (x < TXT_KB_OK_X) ? 302 : 303;
  }
  return -1;
}

void handleTextPadTouch(int16_t x, int16_t y) {
  int hit = textPadHit(x, y);
  if (hit < 0) return;

  if (hit <= 9) {
    if ((int)textBuffer.length() < textMaxLen) textBuffer += textRow0()[hit];
    drawTextField();
  } else if (hit >= 100 && hit <= 108) {
    if ((int)textBuffer.length() < textMaxLen) textBuffer += textRow1()[hit - 100];
    drawTextField();
  } else if (hit >= 200 && hit <= 206) {
    if ((int)textBuffer.length() < textMaxLen) textBuffer += textRow2()[hit - 200];
    drawTextField();
  } else if (hit == 300) { // MODE: cycle lowercase -> uppercase -> symbols -> lowercase
    textMode = (textMode == TXT_LOWER) ? TXT_UPPER : (textMode == TXT_UPPER ? TXT_SYMBOLS : TXT_LOWER);
    drawTextPad();
  } else if (hit == 301) { // DEL
    if (textBuffer.length()) textBuffer.remove(textBuffer.length() - 1);
    drawTextField();
  } else if (hit == 302) { // SPACE
    if ((int)textBuffer.length() < textMaxLen) textBuffer += ' ';
    drawTextField();
  } else if (hit == 303) { // OK
    if (textPurpose == TXT_SSID) {
      pendingSsid = textBuffer;
      startTextEntry(TXT_PASSWORD, 63, currentPass);
    } else {
      saveWifiCreds(pendingSsid, textBuffer);
      if (firstRunActive) {
        // Wi-Fi path considers first-run setup complete once creds are
        // saved — NTP sync and geocoding continue in the background as
        // normal from here (see saveWifiCreds()/loop()).
        markSetupDone();
        firstRunActive = false;
        screen = SCR_CLOCK;
        drawStaticUI();
      } else {
        screen = SCR_SETTINGS;
        drawSettingsMenu();
      }
    }
  }
}

// =========================================================================
// Setup / Loop
// =========================================================================
void setup() {
  Serial.begin(115200); // GPS bring-up diagnostics ([gps] ...) go to the USB Serial Monitor
  loadSettings();

  ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ, BL_PWM_RES);
  ledcAttachPin(LCD_BL, BL_PWM_CHANNEL);
  applyBrightness();

  tftSPI.begin(LCD_SCLK, LCD_MISO, LCD_MOSI, LCD_CS);
  tft.begin();
  tft.setRotation(1);      // landscape
  // NOTE: unlike the ES3C28P board, this panel usually does NOT need
  // tft.invertDisplay(true) — if colors look inverted on your unit, try
  // adding that call here.

  computeClockLayout();
  touchInit();

  if (!setupDone) {
    // First boot: show the Wi-Fi vs. offline choice instead of jumping
    // straight into the normal auto-NTP/auto-geocode boot sequence —
    // neither is meaningful until the user has picked a path.
    firstRunActive = true;
    screen = SCR_FIRST_RUN;
    drawFirstRunScreen();
    return;
  }

  if (gpsMode) {
    // GPS mode: no Wi-Fi, no NTP. The clock stays unset (there's no battery-
    // backed RTC) until the module delivers its first valid fix — loop()
    // feeds the parser and the screen shows "ACQUIRING GPS..." until then.
    // A module with its backup battery warm-starts in seconds; a cold start
    // outdoors is typically well under a minute.
    gpsBegin();
    hasWallClock = false;
    clockIsLocalLabeled = false;
    screen = SCR_CLOCK;
    drawStaticUI();
    return;
  }

  // Bounded attempt at boot — if no network shows up within the timeout,
  // we carry on without one instead of hanging here forever. loop() keeps
  // retrying quietly in the background so a later connection still syncs.
  hasWallClock = attemptNtpSync(15000, 8000, true);
  clockIsLocalLabeled = false; // NTP-synced clock is true UTC
  lastNtpAttemptMs = millis();

  if (hasWallClock) {
    fetchTimezoneFromIP();
    if (!hasCoords) {
      needsGeocode = true;
    } else if (!hasUtcOffset) {
      localUtcOffsetSeconds = (long)lround(longitude / 15.0) * 3600; // rough fallback
      saveUtcOffset();
    }
  }
  // If we don't have a wall clock yet, leave needsGeocode/offset alone —
  // geocoding needs internet too, so both wait for the background retry.

  screen = SCR_CLOCK;
  drawStaticUI();
}

void loop() {
  int16_t x, y;
  bool pressed = touchPressed(x, y);
  gpsPoll(); // no-op unless GPS mode is on; feeds the NMEA parser on every screen

  switch (screen) {
    case SCR_CLOCK:
      if (pressed && x >= GEAR_HIT_X0 && x <= GEAR_HIT_X1 && y >= GEAR_HIT_Y0 && y <= GEAR_HIT_Y1) {
        screen = SCR_SETTINGS;
        drawSettingsMenu();
        break;
      }
      // Background NTP: retry often until we've ever synced, then just
      // occasionally to correct drift. Each attempt is bounded and silent
      // (no "CONNECTING..." screen) so a missing network only stalls the
      // touchscreen briefly and rarely, rather than blocking forever.
      {
        // Skipped entirely in GPS mode, which supplies the clock itself.
        unsigned long retryInterval = hasWallClock ? NTP_RESYNC_INTERVAL_MS : NTP_RETRY_INTERVAL_MS;
        if (!gpsMode && millis() - lastNtpAttemptMs > retryInterval) {
          lastNtpAttemptMs = millis();
          bool hadWallClock = hasWallClock;
          if (attemptNtpSync(6000, 5000, false)) {
            hasWallClock = true;
            clockIsLocalLabeled = false; // NTP-synced clock is true UTC
            fetchTimezoneFromIP();
            if (!hadWallClock) {
              // First time we've ever gotten a real clock this boot —
              // kick off whatever was waiting on it.
              if (!hasCoords) needsGeocode = true;
              else if (!hasUtcOffset) {
                localUtcOffsetSeconds = (long)lround(longitude / 15.0) * 3600;
                saveUtcOffset();
              }
              screenNeedsFullRedraw = true;
            }
          }
        }
      }

      if (needsGeocode) {
        doGeocode();
      } else if (candleLightingEpoch == 0 && hasCoords && hasWallClock) {
        computeShabbatTimes();
      }
      if (millis() - lastDisplayUpdate > 1000) {
        lastDisplayUpdate = millis();
        if (gpsMode) {
          if (hasWallClock) applyCityTableOffset(); // carries the display across a DST changeover
          if (!hasWallClock) {
            // Still waiting for the first fix: repaint the status text only
            // when the satellite count / diagnosis actually changed.
            int k = gpsStatusKey();
            if (k != lastGpsStatusKey) { lastGpsStatusKey = k; screenNeedsFullRedraw = true; }
          }
        }
        if (screenNeedsFullRedraw) {
          drawCountdownFrame();
          screenNeedsFullRedraw = false;
        }
        updateCountdown();
        drawHeaderLabel(); // picks up "GPS SEARCHING" -> position once a fix lands
        drawHeaderTime();
      }
      break;

    case SCR_SETTINGS:
      if (pressed) handleSettingsTouch(x, y);
      break;

    case SCR_KEYPAD:
      if (pressed) handleKeypadTouch(x, y);
      break;

    case SCR_BRIGHTNESS:
      if (pressed) handleBrightnessTouch(x, y);
      break;

    case SCR_TEXTPAD:
      if (pressed) handleTextPadTouch(x, y);
      break;

    case SCR_CITY_LIST:
      if (pressed) handleCityListTouch(x, y);
      break;

    case SCR_FIRST_RUN:
      if (pressed) handleFirstRunTouch(x, y);
      break;

    case SCR_CONFIRM_RESET:
      if (pressed) handleConfirmResetTouch(x, y);
      break;
  }
}
