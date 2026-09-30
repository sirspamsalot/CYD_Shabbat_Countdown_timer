/*
  Shabbat Candle-Lighting / Havdalah Countdown — "Studio 60" style
  CrowPanel ESP32-S3 4.2" E-Paper HMI variant

  ======================================================================
  This is a THIRD hardware target for the same project as
  shabbat_countdown_cloud.ino (ESP32-S3 + ES3C28P touchscreen) and
  shabbat_countdown_cyd/shabbat_countdown_cyd.ino (plain ESP32 + CYD
  touchscreen). The solar-calculated candle-lighting/havdalah math,
  ZIP->lat/lon geocoding, NTP time sync, and Preferences-based persistence
  are IDENTICAL logic, carried over verbatim. Everything else is different,
  because this board is fundamentally different from the other two:

    * E-PAPER, not an LCD. It only looks good when redrawn rarely — a
      screen doing a full-speed per-second ticking countdown like the
      other two boards would flicker/ghost badly and wear the panel out.
      This version instead updates once a minute (no seconds shown), does
      a fast "partial refresh" for that, and does a slower ghost-clearing
      "full refresh" occasionally.
    * NO TOUCHSCREEN. This board has physical buttons (MENU, EXIT) and a
      3-pin rotary control (UP/DOWN/OK) instead. Every screen in this
      sketch is navigated with those 5 inputs, not taps.
    * NO on-device Wi-Fi keyboard. Typing an SSID/password one character
      at a time with only UP/DOWN/OK would be painful, so Wi-Fi setup
      here works differently: selecting WI-FI from the settings menu spins
      up a temporary Wi-Fi hotspot ("ShabbatClock-Setup") with a small web
      page — connect to it from your phone, fill in your real network's
      name and password, submit, done. ZIP code and numeric settings
      (havdalah offset, date/time) ARE just numbers, so those use simple
      UP/DOWN stepper screens instead, no captive portal needed.
    * NO backlight, so there's no BRIGHTNESS setting on this board (the
      panel is purely reflective — it looks the same in any room light,
      which is the whole point of e-paper).

  ======================================================================
  HARDWARE — CrowPanel ESP32-S3 4.2" E-Paper HMI Display (Elecrow)
  Product page: https://www.elecrow.com/wiki/CrowPanel_ESP32_E-paper_4.2-inch_HMI_Display.html

    MCU:            ESP32-S3-WROOM-1-N8R8 (8MB flash, 8MB PSRAM)
                     Board: "ESP32S3 Dev Module" in Arduino, PSRAM enabled
                     ("OPI PSRAM" or "QSPI PSRAM" per your board's silkscreen —
                     check the Elecrow wiki if unsure which PSRAM mode to pick).
    Display:        4.2" 400x300 monochrome e-paper, SSD1683 controller
                     CS=45  DC=46  RST=47  BUSY=48  SCLK=12  MOSI=11
                     PWR (panel power enable, must be driven HIGH) = 7
    Buttons:        MENU=2  EXIT=1
    Rotary control: UP=6  DOWN=4  OK=5
                     NOTE: the vendor's own docs label these pins by
                     logical function (up/down/ok) rather than as raw
                     quadrature A/B lines, so this sketch treats each as
                     an independent momentary input (one press = one
                     step). If your unit's rotary knob instead needs true
                     quadrature decoding to feel right (missed/doubled
                     steps when turning), that's the thing to revisit.
    Library:        GxEPD2 (by Jean-Marc Zingg), display class
                     GxEPD2_420_GDEY042T81 (src/gdey/ subfolder). An earlier
                     version of this sketch referenced GxEPD2_420_GYE042A87,
                     which several third-party CrowPanel write-ups
                     (including Elecrow's own wiki text) cite but which was
                     never actually merged into the GxEPD2 library (only
                     discussed/donated on the GxEPD2 forum, never shipped)
                     — fails to compile ("fatal error: ... No such file or
                     directory"). GDEY042T81 is the SSD1683/400x300 4.2"
                     driver that IS actually in the library, and it's also
                     what the manufacturer's own reference write-up
                     (mischianti.org's CrowPanel ESP32-S3 4.2" article,
                     which mirrors Elecrow's example) uses in practice —
                     same pins (CS=45 DC=46 RST=47 BUSY=48 PWR=7,
                     SCLK=12 MOSI=11, all confirmed against that reference)
                     and the same display.init(115200, true, 50, false)
                     call already used below. That reference also documents
                     BUSY as HIGH=busy / LOW=ready, i.e. the GxEPD2 library's
                     own default — so this sketch deliberately does NOT
                     invert BUSY polarity, even though on one real unit
                     BUSY was observed (via a manual diagnostic) parked HIGH
                     indefinitely after reset and every refresh call timed
                     out ("Busy Timeout!"). An earlier attempt inverted the
                     polarity to make the timeout go away, but that only
                     masked the symptom: it made _waitWhileBusy() return
                     almost instantly (microseconds, not the ~1.2s a real
                     refresh takes) without ever actually waiting for the
                     panel, and the screen never updated. Since the pin
                     mapping and init call both match the manufacturer's own
                     known-working example, a BUSY pin stuck HIGH forever
                     now looks like a hardware-level symptom (panel/cable/
                     power), not a software one — see the troubleshooting
                     note further down.

  IMPORTANT — hardware assumptions flagged for on-device verification,
  same spirit as the CYD sketch's notes: I could not compile-test this
  file against real hardware or the GxEPD2 library from this environment.
  Things most likely to need a tweak on your actual unit:
    - Button polarity: assumed active-LOW with internal pull-ups. If
      presses register backwards (or never register), flip BTN_ACTIVE_LOW
      below.
    - BUSY pin stuck HIGH / "Busy Timeout!" / screen never updates: pins
      and init call are confirmed to match the manufacturer's own
      reference example (see Library note above), so if this still
      happens, it points at the physical panel connection rather than
      code — things worth checking on the actual unit: (1) the e-paper
      panel's FPC ribbon cable fully seated and the connector's latch
      fully closed (a half-seated cable is the single most common cause
      of "everything looks right in software but nothing happens on the
      panel"); (2) try a noticeably longer EPD_PWR settle delay (the
      `delay(50)` after EPD_PWR goes HIGH, below) in case the panel's
      internal boost regulator needs more time under this particular
      unit/cable's load; (3) if neither helps, this may be a defective
      panel/connector and worth raising with Elecrow support.
    - Screen rotation: display.setRotation(1) is a starting guess for
      landscape. Try 0/2/3 if the image is sideways or mirrored.
    - BUSY pin polarity: this sketch currently assumes INVERTED polarity
      (LOW = busy) based on a real-hardware diagnostic showing BUSY parked
      HIGH indefinitely after reset. If timeouts persist even with the
      vendored LOW-polarity driver, the BUSY pin/wiring itself (not just
      polarity) is the more likely culprit — double check EPD_BUSY=48
      against your actual board.

  BEFORE YOU FLASH:
  1. No Wi-Fi credentials to set here at all — flash as-is, then from the
     device: MENU > (rotary down to) WI-FI > OK. Connect your phone to the
     "ShabbatClock-Setup" Wi-Fi network it starts, open http://192.168.4.1,
     enter your real network's name/password, submit.
  2. Libraries (Arduino Library Manager): "GxEPD2", "Adafruit GFX Library",
     "ArduinoJson". WiFi, HTTPClient, WebServer, DNSServer, Preferences,
     SPI ship with the ESP32 core.
  3. Board: "ESP32S3 Dev Module". Flash size 8MB, PSRAM enabled (required —
     the full-buffer GxEPD2 mode needs it), default partition scheme.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_420_GDEY042T81.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include "cities.h" // 250 major world cities, for picking a location without a US ZIP code
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

// These are declared this early for the same reason as the touchscreen
// sketches: the Arduino build system inserts auto-generated function
// prototypes right after the last #include, before these enum types would
// otherwise be visible to functions that take them as parameters.
enum AppScreen { SCR_CLOCK, SCR_SETTINGS, SCR_NUM_ENTRY, SCR_WIFI_SETUP, SCR_CITY_LIST, SCR_FIRST_RUN, SCR_CONFIRM_RESET };
enum NumEntryPurpose { NUM_ZIP, NUM_HAVDALAH, NUM_YEAR, NUM_MONTH, NUM_DAY, NUM_HOUR, NUM_MINUTE };

// ============ USER CONFIG ============
const int CANDLE_LIGHTING_MINUTES = 18;      // minutes before sunset for candle lighting
const char* DEFAULT_ZIP = "89169";           // used only if no ZIP has been saved yet
const int DEFAULT_HAVDALAH_OFFSET_MIN = 42;  // "3 medium stars" — adjustable in Settings
// ======================================

// ---------- CrowPanel ESP32-S3 4.2" E-Paper pins ----------
#define EPD_CS   45
#define EPD_DC   46
#define EPD_RST  47
#define EPD_BUSY 48
#define EPD_SCLK 12
#define EPD_MOSI 11
#define EPD_PWR   7   // must be driven HIGH before talking to the panel

#define BTN_MENU  2
#define BTN_EXIT  1
#define BTN_UP    6
#define BTN_DOWN  4
#define BTN_OK    5
#define BTN_ACTIVE_LOW 1   // flip to 0 if your unit reads the opposite way
// ------------------------------------------------

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
  GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

Preferences prefs;
DNSServer dnsServer;
WebServer webServer(80);

// ---------------- Persisted settings ----------------
String currentZip;
double latitude = 0.0, longitude = 0.0;
bool hasCoords = false;
int havdalahOffsetMin = DEFAULT_HAVDALAH_OFFSET_MIN;
long localUtcOffsetSeconds = 0;
bool hasUtcOffset = false;
String currentSsid = "";
String currentPass = "";
bool hasWifiCreds = false;
bool usingCity = false;      // true if the location came from the CITY list rather than a ZIP
String currentCityLabel = ""; // short "City" name shown in the header when usingCity is true
bool setupDone = false;       // true once first-run setup (Wi-Fi OR offline SET TIME+CITY) has completed once

// ---------------- First-run setup state (not persisted) ----------------
// Set true only while the SCR_FIRST_RUN sequence is in progress, so the
// SET TIME and Wi-Fi-portal flows know to route back into first-run (CITY
// list / SCR_CLOCK) instead of to Settings once they finish.
bool firstRunActive = false;
int firstRunCursor = 0; // 0 = CONNECT TO WI-FI, 1 = SET TIME + CITY (OFFLINE)

// ---------------- Runtime state ----------------
time_t candleLightingEpoch = 0;
time_t havdalahEpoch = 0;
bool needsGeocode = false;
bool screenNeedsFullRedraw = true;

bool hasWallClock = false;
unsigned long lastNtpAttemptMs = 0;
const unsigned long NTP_RETRY_INTERVAL_MS  = 5UL  * 60UL * 1000UL; // retry every 5 min until synced
const unsigned long NTP_RESYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL; // then resync every 6 hrs

unsigned long lastMinuteTick = 0;
unsigned long lastFullRefreshMs = 0;
const unsigned long MINUTE_TICK_MS = 60UL * 1000UL;
const unsigned long FULL_REFRESH_INTERVAL_MS = 30UL * 60UL * 1000UL; // ghost-clearing refresh cadence

AppScreen screen = SCR_CLOCK;
int settingsCursor = 0;
const char* SETTINGS_LABELS[7] = { "ZIP CODE", "CITY", "WI-FI", "SET TIME", "HAVDALAH OFFSET", "SYSTEM RESET", "BACK" };
int confirmResetCursor = 1; // 0 = YES, 1 = CANCEL (default); confirm screen for Settings > SYSTEM RESET

int cityListTop = 0;    // index of the first visible row in the CITY list screen
int cityListCursor = 0; // highlighted row within the visible window (0..CITY_ROWS_VISIBLE-1)

NumEntryPurpose numPurpose = NUM_ZIP;
int numValue = 0, numMin = 0, numMax = 0, numStep = 1;
String numTitle = "";
int zipDigits[5] = {0,0,0,0,0};
int zipCursor = 0;
int tmpYear, tmpMonth, tmpDay, tmpHour, tmpMinute; // scratch for the SET TIME sequence

bool wifiPortalActive = false;

// =========================================================================
// Buttons — simple edge-detected "was this just pressed" reads, one call
// per input per loop() iteration. See BTN_ACTIVE_LOW above if these come
// out inverted on your unit.
// =========================================================================
bool rawPressed(int pin) {
  int level = digitalRead(pin);
  return BTN_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
}

bool edgePressed(int pin, bool &wasDown) {
  bool down = rawPressed(pin);
  bool fresh = down && !wasDown;
  wasDown = down;
  return fresh;
}

bool menuPressed() { static bool w=false; return edgePressed(BTN_MENU, w); }
bool exitPressed() { static bool w=false; return edgePressed(BTN_EXIT, w); }
bool upPressed()   { static bool w=false; return edgePressed(BTN_UP,   w); }
bool downPressed() { static bool w=false; return edgePressed(BTN_DOWN, w); }
bool okPressed()   { static bool w=false; return edgePressed(BTN_OK,   w); }

// =========================================================================
// WiFi / NTP — same bounded, never-blocks-forever, background-retry
// approach as the other two sketches (see their attemptNtpSync notes).
// No on-screen spinner here, since animating one would mean thrashing the
// e-paper panel with rapid partial refreshes while waiting.
// =========================================================================
bool attemptNtpSync(unsigned long wifiTimeoutMs, unsigned long ntpTimeoutMs) {
  if (!hasWifiCreds) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(currentSsid.c_str(), currentPass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > wifiTimeoutMs) {
      WiFi.disconnect(true);
      return false;
    }
    delay(200);
  }
  configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // UTC; epoch math done below
  struct tm timeinfo;
  bool synced = getLocalTime(&timeinfo, ntpTimeoutMs);
  if (!synced) WiFi.disconnect(true);
  return synced;
}

// ---------------- Portable UTC date/time <-> epoch (no timegm dependency) ----------------
// Howard Hinnant's days_from_civil / civil_from_days algorithms — identical
// to the other two sketches, carried over verbatim (hardware-independent).
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
// Standard "Sunrise/Sunset Algorithm" (Nautical Almanac formula) — same as
// the other two sketches. Returns the UTC time of sunset as fractional
// hours (0-24) for a given date/location.
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
  if (cosH > 1) cosH = 1;
  if (cosH < -1) cosH = -1;

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

// ---------------- Compute this/next Shabbat's candle-lighting + havdalah ----------------
void computeShabbatTimes() {
  time_t now = time(nullptr);
  long daysSinceEpoch = now / 86400;
  int weekday = (int)((daysSinceEpoch + 4) % 7);
  int daysSinceFriday = (weekday - 5 + 7) % 7;
  long fridayDays = daysSinceEpoch - daysSinceFriday;

  for (int attempt = 0; attempt < 2; attempt++) {
    int fy, fm, fd;
    civilFromDays(fridayDays, fy, fm, fd);
    time_t fridaySunset = sunsetEpochFor(fy, fm, fd, latitude, longitude);

    int sy, sm, sd;
    civilFromDays(fridayDays + 1, sy, sm, sd);
    time_t satSunset = sunsetEpochFor(sy, sm, sd, latitude, longitude);

    time_t candidateCandle = fridaySunset - (time_t)CANDLE_LIGHTING_MINUTES * 60;
    time_t candidateHavdalah = satSunset + (time_t)havdalahOffsetMin * 60;

    if (now >= candidateHavdalah) {
      fridayDays += 7;
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
  localUtcOffsetSeconds = prefs.getLong("utcOff", 0);
  hasUtcOffset = prefs.getBool("hasUtcOff", false);
  currentSsid = prefs.getString("ssid", "");
  currentPass = prefs.getString("pass", "");
  hasWifiCreds = currentSsid.length() > 0;
  usingCity = prefs.getBool("usingCity", false);
  currentCityLabel = prefs.getString("cityLbl", "");
  setupDone = prefs.getBool("setupDone", false);
  prefs.end();
}

void markSetupDone() {
  setupDone = true;
  prefs.begin("shabbat", false);
  prefs.putBool("setupDone", true);
  prefs.end();
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

void saveZip(const String &zip) {
  prefs.begin("shabbat", false);
  prefs.putString("zip", zip);
  prefs.putBool("usingCity", false); // switch the header back to ZIP display
  prefs.end();
  currentZip = zip;
  usingCity = false;
}

// Picks a location from the built-in CITY_LIST (cities.h) instead of a US
// ZIP code — the ZIP->lat/lon lookup (api.zippopotam.us) only covers US
// ZIP codes, so this is the path for everyone else. No network lookup is
// needed at all, since the list already carries lat/lon.
void selectCity(int idx) {
  if (idx < 0 || idx >= CITY_COUNT) return;
  latitude = CITY_LIST[idx].lat;
  longitude = CITY_LIST[idx].lon;

  String full = CITY_LIST[idx].name;
  int comma = full.indexOf(',');
  currentCityLabel = (comma >= 0) ? full.substring(0, comma) : full;
  currentCityLabel.toUpperCase();
  usingCity = true;

  prefs.begin("shabbat", false);
  prefs.putDouble("lat", latitude);
  prefs.putDouble("lon", longitude);
  prefs.putBool("hasCoords", true);
  prefs.putBool("usingCity", true);
  prefs.putString("cityLbl", currentCityLabel);
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

void saveCoords(double lat, double lon) {
  prefs.begin("shabbat", false);
  prefs.putDouble("lat", lat);
  prefs.putDouble("lon", lon);
  prefs.putBool("hasCoords", true);
  prefs.end();
  hasCoords = true;
}

void saveHavdalahOffset() {
  prefs.begin("shabbat", false);
  prefs.putInt("havOff", havdalahOffsetMin);
  prefs.end();
}

void saveUtcOffset() {
  prefs.begin("shabbat", false);
  prefs.putLong("utcOff", localUtcOffsetSeconds);
  prefs.putBool("hasUtcOff", true);
  prefs.end();
  hasUtcOffset = true;
}

void saveWifiCreds(const String &ssid, const String &pass) {
  prefs.begin("shabbat", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  currentSsid = ssid;
  currentPass = pass;
  hasWifiCreds = currentSsid.length() > 0;
  hasWallClock = attemptNtpSync(15000, 8000);
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
  saveZip(zip);
  needsGeocode = true;
  candleLightingEpoch = 0;
  havdalahEpoch = 0;
}

// =========================================================================
// Drawing helpers (monochrome — GxEPD_BLACK / GxEPD_WHITE only)
// =========================================================================
const GFXfont* chromeFont(uint8_t size) {
  switch (size) {
    case 1:  return &FreeSansBold9pt7b;
    case 2:  return &FreeSansBold12pt7b;
    case 3:  return &FreeSansBold18pt7b;
    default: return &FreeSansBold24pt7b;
  }
}

void printCentered(const char* txt, int cx, int y, uint8_t size, uint16_t fg, uint16_t bg) {
  display.setFont(chromeFont(size));
  display.setTextSize(1);
  display.setTextColor(fg, bg);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(txt, 0, 0, &bx, &by, &bw, &bh);
  display.setCursor(cx - bw / 2 - bx, y - by);
  display.print(txt);
  display.setFont();
}

void drawCandleIcon(int x, int y, uint16_t color) {
  display.fillRect(x, y + 6, 6, 14, color);
  display.fillRect(x - 1, y + 4, 8, 2, color);
  display.fillTriangle(x + 3, y - 6, x, y + 4, x + 6, y + 4, color);
}

// One full-buffer refresh pass: clears to white, calls the given content
// function to draw everything, then pushes the whole panel. Used for every
// screen except the once-a-minute clock tick (which uses a faster partial
// refresh — see partialRefreshClock()).
void fullRefreshGeneric(void (*drawFn)()) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawFn();
  } while (display.nextPage());
}

// =========================================================================
// Screen: main clock
// =========================================================================
#define HEADER_H 32
#define SCREEN_W 400
#define SCREEN_H 300

void drawHeader() {
  display.fillRect(0, 0, SCREEN_W, HEADER_H, GxEPD_BLACK);
  drawCandleIcon(10, 16, GxEPD_WHITE);
  drawCandleIcon(22, 16, GxEPD_WHITE);

  String zipText = usingCity ? currentCityLabel : ("ZIP " + currentZip);
  display.setFont(&FreeSansBold9pt7b);
  display.setTextSize(1);
  display.setTextColor(GxEPD_WHITE, GxEPD_BLACK);
  int16_t zbx, zby; uint16_t zbw, zbh;
  display.getTextBounds(zipText.c_str(), 0, 0, &zbx, &zby, &zbw, &zbh);
  display.setCursor(42 - zbx, 11 - zby);
  display.print(zipText);

  if (hasWallClock) {
    String t = formatLocalTime(time(nullptr));
    int16_t tbx, tby; uint16_t tbw, tbh;
    display.getTextBounds(t.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(392 - tbw - tbx, 11 - tby);
    display.print(t);
  }
  display.setFont();
}

// Builds the two countdown strings ("3 DAYS" / "14 HR 22 MIN") from the
// live clock. No seconds — a once-a-minute e-paper refresh can't usefully
// show them anyway, and refreshing that often would ghost/wear the panel.
void countdownStrings(String &lineTop, String &lineBottom) {
  time_t now = time(nullptr);
  time_t target = (now < candleLightingEpoch) ? candleLightingEpoch : havdalahEpoch;
  long secsLeft = (long)(target - now);
  if (secsLeft < 0) secsLeft = 0;

  int days = secsLeft / 86400;
  int hours = (secsLeft % 86400) / 3600;
  int mins = (secsLeft % 3600) / 60;

  char buf1[16], buf2[24];
  snprintf(buf1, sizeof(buf1), "%d DAY%s", days, days == 1 ? "" : "S");
  snprintf(buf2, sizeof(buf2), "%02d HR %02d MIN", hours, mins);
  lineTop = buf1;
  lineBottom = buf2;
}

void drawCountdownArea() {
  printCentered("TIME REMAINING", 200, 42, 3, GxEPD_BLACK, GxEPD_WHITE);

  const int fx0 = 40, fy0 = 74, fx1 = 360, fy1 = 170;
  display.drawRoundRect(fx0, fy0, fx1 - fx0, fy1 - fy0, 8, GxEPD_BLACK);

  // Unified gating: !hasWallClock covers both "never synced" (online path)
  // AND "never manually set" (offline path) in one check, since either one
  // leaves the clock unusable. The message branches on hasWifiCreds so an
  // offline (manually-set clock, no Wi-Fi) device doesn't wrongly show a
  // Wi-Fi nag once its clock has been set.
  if (!hasWallClock) {
    if (hasWifiCreds) {
      printCentered("SYNCING TIME...", 200, 105, 2, GxEPD_BLACK, GxEPD_WHITE);
      printCentered("(waiting on Wi-Fi/NTP)", 200, 132, 1, GxEPD_BLACK, GxEPD_WHITE);
    } else {
      printCentered("SET UP WI-FI OR SET TIME", 200, 105, 2, GxEPD_BLACK, GxEPD_WHITE);
      printCentered("(MENU > Settings)", 200, 132, 1, GxEPD_BLACK, GxEPD_WHITE);
    }
  } else if (!hasCoords) {
    printCentered("NO LOCATION SET", 200, 105, 2, GxEPD_BLACK, GxEPD_WHITE);
    printCentered("(MENU > Settings)", 200, 132, 1, GxEPD_BLACK, GxEPD_WHITE);
  } else if (needsGeocode) {
    printCentered("LOCATING...", 200, 112, 2, GxEPD_BLACK, GxEPD_WHITE);
  } else if (candleLightingEpoch == 0 || havdalahEpoch == 0) {
    printCentered("CALCULATING...", 200, 112, 2, GxEPD_BLACK, GxEPD_WHITE);
  } else {
    String lineTop, lineBottom;
    countdownStrings(lineTop, lineBottom);

    // Vertically center the two-line countdown as one block inside the frame.
    int frameH = fy1 - fy0;
    int lineH1 = 32, lineH2 = 24, gap = 6; // approx text heights at size 4 / size 3
    int blockH = lineH1 + lineH2 + gap;
    int blockTop = fy0 + (frameH - blockH) / 2;
    printCentered(lineTop.c_str(), 200, blockTop, 4, GxEPD_BLACK, GxEPD_WHITE);
    printCentered(lineBottom.c_str(), 200, blockTop + lineH1 + gap, 3, GxEPD_BLACK, GxEPD_WHITE);
  }

  if (hasWallClock && !needsGeocode && candleLightingEpoch != 0 && havdalahEpoch != 0) {
    String candleLine = "CANDLES " + formatLocalTime(candleLightingEpoch);
    String havdalahLine = "HAVDALAH " + formatLocalTime(havdalahEpoch);

    // Same "center the whole block" approach as the touchscreen sketches.
    int areaTop = fy1 + 10;
    int areaBottom = 266;
    int areaHeight = areaBottom - areaTop;
    int lineH = 17, gap = 8;
    int blockH = lineH * 2 + gap;
    int blockTop = areaTop + (areaHeight - blockH) / 2;

    printCentered(candleLine.c_str(), 200, blockTop, 2, GxEPD_BLACK, GxEPD_WHITE);
    printCentered(havdalahLine.c_str(), 200, blockTop + lineH + gap, 2, GxEPD_BLACK, GxEPD_WHITE);
  }
}

void drawFooterHint() {
  printCentered("MENU: Settings", 200, 282, 1, GxEPD_BLACK, GxEPD_WHITE);
}

void clockFullContent() {
  drawHeader();
  drawCountdownArea();
  drawFooterHint();
}

void clockPartialContent() {
  display.fillRect(0, 0, SCREEN_W, 270, GxEPD_WHITE);
  drawHeader();
  drawCountdownArea();
}

void fullRefreshClock() {
  fullRefreshGeneric(clockFullContent);
  lastFullRefreshMs = millis();
}

void partialRefreshClock() {
  display.setPartialWindow(0, 0, SCREEN_W, 270);
  display.firstPage();
  do {
    clockPartialContent();
  } while (display.nextPage());
}

// ---------------- One-off status screens (locating/failed), full refresh ----------------
void locatingContent() { printCentered("LOCATING...", 200, 130, 2, GxEPD_BLACK, GxEPD_WHITE); }
void zipFailContent() {
  printCentered("ZIP LOOKUP FAILED", 200, 120, 2, GxEPD_BLACK, GxEPD_WHITE);
  printCentered("Check ZIP in Settings & retry", 200, 150, 1, GxEPD_BLACK, GxEPD_WHITE);
}
void connectingContent() { printCentered("CONNECTING TO WI-FI...", 200, 130, 2, GxEPD_BLACK, GxEPD_WHITE); }

void doGeocode() {
  fullRefreshGeneric(locatingContent);
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
  } else {
    fullRefreshGeneric(zipFailContent);
    delay(3000);
    needsGeocode = false;
  }
  screenNeedsFullRedraw = true;
}

void updateCountdown() {
  if (needsGeocode || candleLightingEpoch == 0 || havdalahEpoch == 0) return;
  time_t now = time(nullptr);
  if (now >= havdalahEpoch) computeShabbatTimes();
}

// =========================================================================
// Screen: settings menu (UP/DOWN to move, OK to select, EXIT to go back)
// =========================================================================
void settingsContent() {
  display.fillRect(0, 0, SCREEN_W, 26, GxEPD_BLACK);
  printCentered("SETTINGS", 200, 4, 1, GxEPD_WHITE, GxEPD_BLACK);

  // ZIP CODE and CITY render as two side-by-side half-width boxes in one
  // row — a visual cue that they're alternatives, not two separate
  // settings — which frees up more spacing for the rows below. UP/DOWN
  // still cycles through all 7 options in order (settingsCursor 0-6); ZIP
  // and CITY just share a row, with whichever one is current highlighted.
  // rowH/rowStep are trimmed slightly from their original 32/44 so the new
  // SYSTEM RESET row still fits above the footer hint at y=286.
  const int top = 30, rowH = 30, rowStep = 40;

  bool selZip = (settingsCursor == 0);
  if (selZip) display.fillRoundRect(20, top, 175, rowH, 6, GxEPD_BLACK);
  else display.drawRoundRect(20, top, 175, rowH, 6, GxEPD_BLACK);
  printCentered("ZIP CODE", 107, top + 6, 2, selZip ? GxEPD_WHITE : GxEPD_BLACK, selZip ? GxEPD_BLACK : GxEPD_WHITE);

  bool selCity = (settingsCursor == 1);
  if (selCity) display.fillRoundRect(205, top, 175, rowH, 6, GxEPD_BLACK);
  else display.drawRoundRect(205, top, 175, rowH, 6, GxEPD_BLACK);
  printCentered("CITY", 292, top + 6, 2, selCity ? GxEPD_WHITE : GxEPD_BLACK, selCity ? GxEPD_BLACK : GxEPD_WHITE);

  for (int i = 2; i < 7; i++) {
    int y = top + (i - 1) * rowStep;
    bool selected = (i == settingsCursor);
    uint16_t fg = selected ? GxEPD_WHITE : GxEPD_BLACK;
    uint16_t bg = selected ? GxEPD_BLACK : GxEPD_WHITE;
    if (selected) display.fillRoundRect(30, y, 340, rowH, 6, GxEPD_BLACK);
    else display.drawRoundRect(30, y, 340, rowH, 6, GxEPD_BLACK);
    printCentered(SETTINGS_LABELS[i], 200, y + 6, 2, fg, bg);
  }
  printCentered("UP/DOWN move   OK select   EXIT back", 200, 286, 1, GxEPD_BLACK, GxEPD_WHITE);
}

void drawSettingsScreen() { fullRefreshGeneric(settingsContent); }

void startZipEntry() {
  String z = (currentZip.length() == 5) ? currentZip : String(DEFAULT_ZIP);
  for (int i = 0; i < 5; i++) zipDigits[i] = z[i] - '0';
  zipCursor = 0;
  numPurpose = NUM_ZIP;
  screen = SCR_NUM_ENTRY;
  drawZipEntryScreen();
}

void zipEntryContent() {
  printCentered("ENTER ZIP CODE", 200, 20, 2, GxEPD_BLACK, GxEPD_WHITE);
  int boxW = 44, boxGap = 10;
  int startX = 200 - (5 * boxW + 4 * boxGap) / 2;
  for (int i = 0; i < 5; i++) {
    int bx = startX + i * (boxW + boxGap);
    bool sel = (i == zipCursor);
    if (sel) display.fillRoundRect(bx, 90, boxW, 60, 6, GxEPD_BLACK);
    else display.drawRoundRect(bx, 90, boxW, 60, 6, GxEPD_BLACK);
    char c[2] = { (char)('0' + zipDigits[i]), 0 };
    printCentered(c, bx + boxW / 2, 108, 3, sel ? GxEPD_WHITE : GxEPD_BLACK, sel ? GxEPD_BLACK : GxEPD_WHITE);
  }
  printCentered("UP/DOWN change digit   OK next   EXIT cancel", 200, 250, 1, GxEPD_BLACK, GxEPD_WHITE);
}

void drawZipEntryScreen() { fullRefreshGeneric(zipEntryContent); }

void handleZipEntryButtons() {
  if (upPressed())   { zipDigits[zipCursor] = (zipDigits[zipCursor] + 1) % 10; drawZipEntryScreen(); }
  if (downPressed()) { zipDigits[zipCursor] = (zipDigits[zipCursor] + 9) % 10; drawZipEntryScreen(); }
  if (exitPressed()) { screen = SCR_SETTINGS; drawSettingsScreen(); }
  if (okPressed()) {
    zipCursor++;
    if (zipCursor >= 5) {
      char buf[6];
      for (int i = 0; i < 5; i++) buf[i] = '0' + zipDigits[i];
      buf[5] = 0;
      saveZipAndTriggerGeocode(String(buf));
      screen = SCR_CLOCK;
      screenNeedsFullRedraw = true;
    } else {
      drawZipEntryScreen();
    }
  }
}

// ---------------- Generic single-value stepper (havdalah offset, date/time fields) ----------------
void startNumEntry(NumEntryPurpose p, const String &title, int value, int mn, int mx, int step) {
  numPurpose = p;
  numTitle = title;
  numValue = value; numMin = mn; numMax = mx; numStep = step;
  screen = SCR_NUM_ENTRY;
  drawNumEntryScreen();
}

void numEntryContent() {
  printCentered(numTitle.c_str(), 200, 30, 2, GxEPD_BLACK, GxEPD_WHITE);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", numValue);
  display.drawRoundRect(120, 90, 160, 70, 10, GxEPD_BLACK);
  printCentered(buf, 200, 108, 4, GxEPD_BLACK, GxEPD_WHITE);
  printCentered("UP/DOWN change   OK confirm   EXIT cancel", 200, 250, 1, GxEPD_BLACK, GxEPD_WHITE);
}

void drawNumEntryScreen() { fullRefreshGeneric(numEntryContent); }

void startHavdalahEntry() {
  startNumEntry(NUM_HAVDALAH, "HAVDALAH OFFSET (MIN)", havdalahOffsetMin, 0, 180, 1);
}

// Used only as a starting point for the offline SET TIME stepper when the
// clock has never been set (hasWallClock false) — parses the compile-time
// __DATE__ macro ("Mon DD YYYY") instead of seeding from time(nullptr),
// which would show the 1970 epoch, so the stepper starts somewhere roughly
// current instead of at garbage.
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

void startSetTimeEntry() {
  if (hasWallClock) {
    // Seed from the device's current best-guess local time as a starting point.
    time_t nowLocal = time(nullptr) + localUtcOffsetSeconds;
    struct tm tmv;
    gmtime_r(&nowLocal, &tmv); // treat as UTC struct since the offset's already folded in
    tmpYear = tmv.tm_year + 1900;
    tmpMonth = tmv.tm_mon + 1;
    tmpDay = tmv.tm_mday;
    tmpHour = tmv.tm_hour;
    tmpMinute = tmv.tm_min;
  } else {
    // No wall clock yet (offline first-run path) — time(nullptr) is still
    // the 1970 epoch, so seed from the firmware's own build date instead.
    compileBuildDate(tmpYear, tmpMonth, tmpDay);
    tmpHour = 18;
    tmpMinute = 0;
  }
  startNumEntry(NUM_YEAR, "ENTER YEAR", tmpYear, 2024, 2100, 1);
}

void handleNumEntryButtons() {
  if (numPurpose == NUM_ZIP) { handleZipEntryButtons(); return; }

  if (upPressed())   { numValue = min(numMax, numValue + numStep); drawNumEntryScreen(); }
  if (downPressed()) { numValue = max(numMin, numValue - numStep); drawNumEntryScreen(); }
  if (exitPressed()) { screen = SCR_SETTINGS; drawSettingsScreen(); }
  if (okPressed()) {
    switch (numPurpose) {
      case NUM_HAVDALAH:
        havdalahOffsetMin = numValue;
        saveHavdalahOffset();
        if (hasCoords) computeShabbatTimes();
        screen = SCR_SETTINGS; drawSettingsScreen();
        break;
      case NUM_YEAR:
        tmpYear = numValue;
        startNumEntry(NUM_MONTH, "ENTER MONTH (1-12)", tmpMonth, 1, 12, 1);
        break;
      case NUM_MONTH:
        tmpMonth = numValue;
        startNumEntry(NUM_DAY, "ENTER DAY (1-31)", tmpDay, 1, 31, 1);
        break;
      case NUM_DAY:
        tmpDay = numValue;
        startNumEntry(NUM_HOUR, "ENTER HOUR (0-23)", tmpHour, 0, 23, 1);
        break;
      case NUM_HOUR:
        tmpHour = numValue;
        startNumEntry(NUM_MINUTE, "ENTER MINUTE (0-59)", tmpMinute, 0, 59, 1);
        break;
      case NUM_MINUTE: {
        tmpMinute = numValue;
        if (hasWifiCreds) {
          // Online path: derive the local UTC offset by comparing the
          // entered local time against the NTP-verified system clock.
          time_t enteredLocalEpoch = utcToEpoch(tmpYear, tmpMonth, tmpDay, tmpHour, tmpMinute, 0);
          time_t nowUtc = time(nullptr);
          localUtcOffsetSeconds = (long)(enteredLocalEpoch - nowUtc);
          saveUtcOffset();
        } else {
          // Offline path: no NTP reference exists at all, so set the
          // system clock directly to the entered LOCAL time and keep the
          // offset at 0 — a self-consistent epoch+offset pair is all
          // formatLocalTime()/computeShabbatTimes() need.
          setSystemTimeManually(tmpYear, tmpMonth, tmpDay, tmpHour, tmpMinute);
          localUtcOffsetSeconds = 0;
          saveUtcOffset();
          hasWallClock = true;
        }
        if (hasCoords) computeShabbatTimes();
        if (firstRunActive) {
          cityListTop = 0; cityListCursor = 0; screen = SCR_CITY_LIST; drawCityListScreen();
        } else {
          screen = SCR_SETTINGS; drawSettingsScreen();
        }
        break;
      }
      default: break;
    }
  }
}

void handleSettingsButtons() {
  if (upPressed())   { settingsCursor = (settingsCursor + 6) % 7; drawSettingsScreen(); }
  if (downPressed()) { settingsCursor = (settingsCursor + 1) % 7; drawSettingsScreen(); }
  if (exitPressed()) { screen = SCR_CLOCK; screenNeedsFullRedraw = true; }
  if (okPressed()) {
    switch (settingsCursor) {
      case 0: startZipEntry(); break;
      case 1: cityListTop = 0; cityListCursor = 0; screen = SCR_CITY_LIST; drawCityListScreen(); break;
      case 2: screen = SCR_WIFI_SETUP; startWifiSetupPortal(); break;
      case 3: startSetTimeEntry(); break;
      case 4: startHavdalahEntry(); break;
      case 5: confirmResetCursor = 1; screen = SCR_CONFIRM_RESET; drawConfirmResetScreen(); break;
      case 6: screen = SCR_CLOCK; screenNeedsFullRedraw = true; break;
    }
  }
}

// =========================================================================
// Screen: SYSTEM RESET confirmation — reachable from Settings. Wipes every
// persisted key (Wi-Fi creds, location, time offset, havdalah offset, etc.)
// and drops the device back into the SCR_FIRST_RUN flow, same as a fresh
// unconfigured boot. UP/DOWN moves the highlight between YES/CANCEL, OK
// confirms the highlighted choice, EXIT cancels outright.
// =========================================================================
void confirmResetContent() {
  display.fillRect(0, 0, SCREEN_W, 26, GxEPD_BLACK);
  printCentered("SYSTEM RESET", 200, 4, 1, GxEPD_WHITE, GxEPD_BLACK);

  printCentered("ARE YOU SURE?", 200, 50, 2, GxEPD_BLACK, GxEPD_WHITE);
  printCentered("erases wifi, location, time", 200, 90, 1, GxEPD_BLACK, GxEPD_WHITE);
  printCentered("and all settings", 200, 108, 1, GxEPD_BLACK, GxEPD_WHITE);

  bool selYes = (confirmResetCursor == 0);
  if (selYes) display.fillRoundRect(60, 160, 140, 60, 8, GxEPD_BLACK);
  else display.drawRoundRect(60, 160, 140, 60, 8, GxEPD_BLACK);
  printCentered("YES, RESET", 130, 182, 1, selYes ? GxEPD_WHITE : GxEPD_BLACK, selYes ? GxEPD_BLACK : GxEPD_WHITE);

  bool selNo = (confirmResetCursor == 1);
  if (selNo) display.fillRoundRect(220, 160, 140, 60, 8, GxEPD_BLACK);
  else display.drawRoundRect(220, 160, 140, 60, 8, GxEPD_BLACK);
  printCentered("CANCEL", 290, 182, 1, selNo ? GxEPD_WHITE : GxEPD_BLACK, selNo ? GxEPD_BLACK : GxEPD_WHITE);

  printCentered("UP/DOWN choose   OK confirm   EXIT cancel", 200, 286, 1, GxEPD_BLACK, GxEPD_WHITE);
}

void drawConfirmResetScreen() { fullRefreshGeneric(confirmResetContent); }

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
  localUtcOffsetSeconds = 0;
  hasUtcOffset = false;
  currentSsid = "";
  currentPass = "";
  hasWifiCreds = false;
  usingCity = false;
  currentCityLabel = "";
  setupDone = false;
  candleLightingEpoch = 0;
  havdalahEpoch = 0;
  hasWallClock = false;
  needsGeocode = false;
  screenNeedsFullRedraw = true;

  firstRunActive = true;
  screen = SCR_FIRST_RUN;
  drawFirstRunScreen();
}

void handleConfirmResetButtons() {
  if (upPressed() || downPressed()) {
    confirmResetCursor = 1 - confirmResetCursor;
    drawConfirmResetScreen();
  }
  if (exitPressed()) {
    screen = SCR_SETTINGS; drawSettingsScreen();
  }
  if (okPressed()) {
    if (confirmResetCursor == 0) {
      performSystemReset();
    } else {
      screen = SCR_SETTINGS; drawSettingsScreen();
    }
  }
}

// =========================================================================
// Screen: CITY list — an alternative to typing a ZIP code, since the ZIP
// lookup only covers the US. UP/DOWN scrolls continuously through all 250
// cities (moving the highlighted row, then paging once it hits the top or
// bottom of the visible window), OK selects, EXIT cancels.
// =========================================================================
#define CITY_ROWS_VISIBLE 8
#define CITY_ROW_H 30

void cityListContent() {
  display.fillRect(0, 0, SCREEN_W, 26, GxEPD_BLACK);
  printCentered("SELECT CITY", 200, 4, 1, GxEPD_WHITE, GxEPD_BLACK);

  for (int i = 0; i < CITY_ROWS_VISIBLE; i++) {
    int idx = cityListTop + i;
    if (idx >= CITY_COUNT) break;
    int y = 30 + i * CITY_ROW_H;
    bool sel = (i == cityListCursor);
    if (sel) display.fillRoundRect(20, y, 360, CITY_ROW_H - 3, 5, GxEPD_BLACK);
    else display.drawRoundRect(20, y, 360, CITY_ROW_H - 3, 5, GxEPD_BLACK);
    printCentered(CITY_LIST[idx].name, 200, y + 6, 1, sel ? GxEPD_WHITE : GxEPD_BLACK, sel ? GxEPD_BLACK : GxEPD_WHITE);
  }
  printCentered("UP/DOWN scroll   OK select   EXIT back", 200, 286, 1, GxEPD_BLACK, GxEPD_WHITE);
}

void drawCityListScreen() { fullRefreshGeneric(cityListContent); }

void handleCityListButtons() {
  if (upPressed()) {
    if (cityListCursor > 0) cityListCursor--;
    else if (cityListTop > 0) cityListTop--;
    drawCityListScreen();
  }
  if (downPressed()) {
    int idx = cityListTop + cityListCursor;
    if (cityListCursor < CITY_ROWS_VISIBLE - 1 && idx + 1 < CITY_COUNT) cityListCursor++;
    else if (idx + 1 < CITY_COUNT) cityListTop++;
    drawCityListScreen();
  }
  if (exitPressed()) { screen = SCR_SETTINGS; drawSettingsScreen(); }
  if (okPressed()) {
    int idx = cityListTop + cityListCursor;
    if (idx < CITY_COUNT) {
      selectCity(idx);
      screen = SCR_CLOCK;
      screenNeedsFullRedraw = true;
    }
  }
}

// =========================================================================
// Screen: Wi-Fi setup — temporary hotspot + a one-page captive portal,
// instead of an on-device keyboard (see the header comment for why).
// =========================================================================
void wifiSetupContent() {
  printCentered("WI-FI SETUP", 200, 20, 2, GxEPD_BLACK, GxEPD_WHITE);
  printCentered("1. On your phone, join Wi-Fi network:", 200, 70, 1, GxEPD_BLACK, GxEPD_WHITE);
  printCentered("ShabbatClock-Setup", 200, 92, 3, GxEPD_BLACK, GxEPD_WHITE);
  printCentered("2. Open a browser to:", 200, 150, 1, GxEPD_BLACK, GxEPD_WHITE);
  printCentered("http://192.168.4.1", 200, 172, 3, GxEPD_BLACK, GxEPD_WHITE);
  printCentered("3. Enter your real network's name & password", 200, 226, 1, GxEPD_BLACK, GxEPD_WHITE);
  printCentered("Press EXIT to cancel", 200, 250, 1, GxEPD_BLACK, GxEPD_WHITE);
}

void handlePortalRoot() {
  String html =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Shabbat Clock Wi-Fi Setup</title></head>"
    "<body style='font-family:sans-serif;max-width:420px;margin:40px auto;padding:0 16px'>"
    "<h2>Shabbat Clock Wi-Fi Setup</h2>"
    "<form method='POST' action='/save'>"
    "<label>Network name (SSID)</label><br>"
    "<input name='ssid' style='width:100%;padding:10px;margin:8px 0;box-sizing:border-box'><br>"
    "<label>Password</label><br>"
    "<input name='pass' type='password' style='width:100%;padding:10px;margin:8px 0;box-sizing:border-box'><br>"
    "<button type='submit' style='padding:12px 24px;margin-top:8px'>Save &amp; Connect</button>"
    "</form></body></html>";
  webServer.send(200, "text/html", html);
}

void handlePortalSave() {
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("pass");
  webServer.send(200, "text/html",
    "<!doctype html><html><body style='font-family:sans-serif;text-align:center;margin-top:60px'>"
    "<h2>Saved</h2><p>The clock will now try to connect. You can close this page.</p>"
    "</body></html>");
  stopWifiSetupPortal();
  saveWifiCreds(ssid, pass); // switches back to STA mode internally and tries NTP right away
  if (firstRunActive) {
    // Wi-Fi path considers first-run setup complete once creds are saved —
    // NTP sync and geocoding continue in the background as normal from
    // here (see saveWifiCreds()/loop()).
    markSetupDone();
    firstRunActive = false;
    screen = SCR_CLOCK;
    screenNeedsFullRedraw = true;
  } else {
    screen = SCR_SETTINGS;
    drawSettingsScreen();
  }
}

void startWifiSetupPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ShabbatClock-Setup");
  dnsServer.start(53, "*", WiFi.softAPIP()); // redirect all DNS lookups to us (captive-portal behavior)
  webServer.on("/", HTTP_GET, handlePortalRoot);
  webServer.on("/save", HTTP_POST, handlePortalSave);
  webServer.onNotFound(handlePortalRoot);
  webServer.begin();
  wifiPortalActive = true;
  fullRefreshGeneric(wifiSetupContent);
}

void stopWifiSetupPortal() {
  webServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  wifiPortalActive = false;
}

void handleWifiSetupLoop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  if (exitPressed()) {
    stopWifiSetupPortal();
    screen = SCR_SETTINGS;
    drawSettingsScreen();
  }
}

// =========================================================================
// Screen: first-run setup choice — shown once, before setupDone is true.
// Offers the existing Wi-Fi flow (auto time via NTP; location is set
// separately in Settings, or defaults to Las Vegas) or a fully offline
// path (manual SET TIME + pick-a-city, no network ever required).
// UP/DOWN toggles which of the two options is highlighted, OK selects it.
// =========================================================================
void firstRunContent() {
  display.fillRect(0, 0, SCREEN_W, 26, GxEPD_BLACK);
  printCentered("WELCOME - SET UP CLOCK", 200, 4, 1, GxEPD_WHITE, GxEPD_BLACK);

  bool sel0 = (firstRunCursor == 0);
  if (sel0) display.fillRoundRect(40, 50, 320, 90, 10, GxEPD_BLACK);
  else display.drawRoundRect(40, 50, 320, 90, 10, GxEPD_BLACK);
  printCentered("CONNECT TO WI-FI", 200, 75, 2, sel0 ? GxEPD_WHITE : GxEPD_BLACK, sel0 ? GxEPD_BLACK : GxEPD_WHITE);
  printCentered("auto time, set location after", 200, 108, 1, sel0 ? GxEPD_WHITE : GxEPD_BLACK, sel0 ? GxEPD_BLACK : GxEPD_WHITE);

  bool sel1 = (firstRunCursor == 1);
  if (sel1) display.fillRoundRect(40, 160, 320, 90, 10, GxEPD_BLACK);
  else display.drawRoundRect(40, 160, 320, 90, 10, GxEPD_BLACK);
  printCentered("SET TIME + CITY", 200, 185, 2, sel1 ? GxEPD_WHITE : GxEPD_BLACK, sel1 ? GxEPD_BLACK : GxEPD_WHITE);
  printCentered("offline, no network needed", 200, 218, 1, sel1 ? GxEPD_WHITE : GxEPD_BLACK, sel1 ? GxEPD_BLACK : GxEPD_WHITE);

  printCentered("UP/DOWN choose   OK select", 200, 286, 1, GxEPD_BLACK, GxEPD_WHITE);
}

void drawFirstRunScreen() { fullRefreshGeneric(firstRunContent); }

void handleFirstRunButtons() {
  if (upPressed() || downPressed()) {
    firstRunCursor = 1 - firstRunCursor;
    drawFirstRunScreen();
  }
  if (okPressed()) {
    firstRunActive = true;
    if (firstRunCursor == 0) {
      screen = SCR_WIFI_SETUP;
      startWifiSetupPortal();
    } else {
      startSetTimeEntry();
    }
  }
}

// =========================================================================
// Setup / Loop
// =========================================================================
void setup() {
  // Diagnostic serial output — if the panel ever shows only a blank/stale
  // screen after flashing, open the Serial Monitor at 115200 baud and see
  // exactly which of these lines is the LAST one printed. That pinpoints
  // whether it's hanging in display.init() versus something later (e.g. it
  // inits fine but never reaches the first draw). On this board the panel
  // itself is confirmed working (it ran a demo cycling through several
  // screens before this sketch was flashed), so a stuck/unresponsive panel
  // after flashing points at this firmware's own init sequence inheriting
  // a bad state from whatever ran before it, not a hardware fault.
  Serial.begin(115200);
  delay(300); // give the USB-CDC serial port a moment to enumerate
  Serial.println(F("[boot] starting setup()"));

  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_EXIT, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);

  pinMode(EPD_PWR, OUTPUT);
  // Force a REAL power cycle rather than just asserting HIGH. If EPD_PWR was
  // already HIGH when this firmware started (e.g. the panel was mid-refresh
  // under a previous/demo firmware when the board got reflashed), a bare
  // digitalWrite(HIGH) is a no-op and the panel's regulator never actually
  // resets — it just stays in whatever state the old firmware left it in.
  // Confirmed known-working demo firmware on this exact unit drove the
  // panel through multiple screens fine, so the panel/cable are not the
  // problem; this targets "our firmware inherited a stuck panel state from
  // the previous firmware" instead.
  digitalWrite(EPD_PWR, LOW);
  delay(200); // hold power off long enough for the regulator to fully discharge
  digitalWrite(EPD_PWR, HIGH); // now a genuine off->on transition
  delay(200); // let the panel's regulator settle before driving SPI/RST into it
  Serial.println(F("[boot] EPD_PWR power-cycled (LOW then HIGH), regulator should be up"));

  SPI.begin(EPD_SCLK, -1 /* MISO unused by the display */, EPD_MOSI, EPD_CS);

  // ---- TEMPORARY DIAGNOSTIC: raw BUSY pin behavior ----
  // Bypasses GxEPD2 entirely and pulses RST by hand, then watches the raw
  // EPD_BUSY level for 2 seconds. This tells us what the panel's BUSY line
  // is actually doing, independent of which GxEPD2 driver class is used —
  // if it never changes at all, that points to wrong pin/wiring; if it
  // toggles but settles "backwards" from what's expected (documented as
  // HIGH=busy, LOW=ready for this panel), that confirms inverted polarity.
  // Safe to delete this whole block once the real cause is found.
  pinMode(EPD_RST, OUTPUT);
  pinMode(EPD_BUSY, INPUT);
  Serial.print(F("[diag] EPD_BUSY before reset: ")); Serial.println(digitalRead(EPD_BUSY));
  // Reset sequence copied EXACTLY from Elecrow's own official demo firmware
  // for this board (EPD_RESET() in their EPD.cpp, from their GitHub repo
  // Elecrow-RD/ESP32_S3-Ink-Screen). Their version explicitly asserts RST
  // HIGH first and lets it settle for 100ms BEFORE pulsing LOW — our
  // previous diagnostic pulsed straight to LOW with no prior HIGH settle,
  // which may not register as a clean reset edge if the pin's power-up
  // state was ambiguous. This is their exact HIGH(100ms)/LOW(10ms)/HIGH(10ms)
  // timing, not a guess.
  digitalWrite(EPD_RST, HIGH);
  delay(100);
  digitalWrite(EPD_RST, LOW);
  delay(10);
  digitalWrite(EPD_RST, HIGH);
  delay(10);
  Serial.println(F("[diag] watching EPD_BUSY for 2s after a manual reset pulse (only level CHANGES are printed):"));
  {
    unsigned long diagStart = millis();
    int lastLevel = -1;
    while (millis() - diagStart < 2000) {
      int level = digitalRead(EPD_BUSY);
      if (level != lastLevel) {
        Serial.print(F("[diag]   t="));
        Serial.print(millis() - diagStart);
        Serial.print(F("ms BUSY="));
        Serial.println(level);
        lastLevel = level;
      }
    }
  }
  Serial.println(F("[diag] done watching BUSY — if nothing printed above besides the first line, BUSY never changed at all"));
  // ---- end diagnostic ----

  Serial.println(F("[boot] SPI.begin() done, calling display.init()..."));
  // reset_duration=50 (vs. the library default's shorter pulse): a
  // community-tested working setup for this exact panel uses a 50ms reset
  // pulse. A too-short reset can leave the panel in a state where BUSY
  // never clears, producing "Busy Timeout!" on the very first refresh —
  // which is the exact symptom this was added to fix.
  display.init(115200, true, 50, false);
  Serial.println(F("[boot] display.init() returned — if you never saw this line, it hung waiting on the panel (BUSY pin most likely)"));
  display.setRotation(1); // landscape; try 0/2/3 if the image is sideways/mirrored on your unit

  loadSettings();
  Serial.println(F("[boot] settings loaded, proceeding to first screen"));

  if (!setupDone) {
    // First boot: show the Wi-Fi vs. offline choice instead of jumping
    // straight into the normal auto-NTP/auto-geocode boot sequence —
    // neither is meaningful until the user has picked a path.
    firstRunActive = true;
    screen = SCR_FIRST_RUN;
    Serial.println(F("[boot] calling drawFirstRunScreen()..."));
    drawFirstRunScreen();
    Serial.println(F("[boot] drawFirstRunScreen() returned — a full panel refresh should have just happened. If the screen still looks unchanged, the refresh itself (fullRefreshGeneric's firstPage/nextPage loop) is where it's stuck or silently failing."));
    return;
  }

  if (hasWifiCreds) {
    fullRefreshGeneric(connectingContent);
    hasWallClock = attemptNtpSync(15000, 8000);
  } else {
    hasWallClock = false;
  }
  lastNtpAttemptMs = millis();

  if (hasWallClock) {
    fetchTimezoneFromIP();
    if (!hasCoords) {
      needsGeocode = true;
    } else if (!hasUtcOffset) {
      localUtcOffsetSeconds = (long)lround(longitude / 15.0) * 3600;
      saveUtcOffset();
    }
  }

  screen = SCR_CLOCK;
  screenNeedsFullRedraw = true;
}

void loop() {
  switch (screen) {
    case SCR_CLOCK: {
      if (menuPressed()) {
        screen = SCR_SETTINGS;
        settingsCursor = 0;
        drawSettingsScreen();
        break;
      }

      // Background NTP: bounded, retried on an interval, no UI while
      // waiting — same shape as the touchscreen sketches' loop(), just
      // without a "CONNECTING..." screen (see attemptNtpSync's note).
      unsigned long retryInterval = hasWallClock ? NTP_RESYNC_INTERVAL_MS : NTP_RETRY_INTERVAL_MS;
      if (hasWifiCreds && millis() - lastNtpAttemptMs > retryInterval) {
        lastNtpAttemptMs = millis();
        bool hadWallClock = hasWallClock;
        if (attemptNtpSync(6000, 5000)) {
          hasWallClock = true;
          fetchTimezoneFromIP();
          if (!hadWallClock) {
            if (!hasCoords) needsGeocode = true;
            else if (!hasUtcOffset) {
              localUtcOffsetSeconds = (long)lround(longitude / 15.0) * 3600;
              saveUtcOffset();
            }
            screenNeedsFullRedraw = true;
          }
        }
      }

      if (needsGeocode) {
        doGeocode();
      } else if (candleLightingEpoch == 0 && hasCoords && hasWallClock) {
        computeShabbatTimes();
      }

      if (screenNeedsFullRedraw) {
        updateCountdown();
        fullRefreshClock();
        screenNeedsFullRedraw = false;
        lastMinuteTick = millis();
      } else if (millis() - lastMinuteTick > MINUTE_TICK_MS) {
        lastMinuteTick = millis();
        updateCountdown();
        if (millis() - lastFullRefreshMs > FULL_REFRESH_INTERVAL_MS) {
          fullRefreshClock(); // periodic ghost-clearing full refresh
        } else {
          partialRefreshClock();
        }
      }
      break;
    }

    case SCR_SETTINGS:  handleSettingsButtons();  break;
    case SCR_NUM_ENTRY: handleNumEntryButtons();  break;
    case SCR_WIFI_SETUP: handleWifiSetupLoop();   break;
    case SCR_CITY_LIST:  handleCityListButtons(); break;
    case SCR_FIRST_RUN:  handleFirstRunButtons(); break;
    case SCR_CONFIRM_RESET: handleConfirmResetButtons(); break;
  }

  delay(20); // light debounce between reads
}
