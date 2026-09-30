/*
  Shabbat Candle-Lighting / Havdalah Countdown — "Studio 60" style
  CYD ("Cheap Yellow Display", Sunton ESP32-2432S028R) variant

  ======================================================================
  This is the CYD-adapted build of the ESP32-S3/ES3C28P sketch. Only the
  hardware layer differs — pin assignments and the touch driver, swapped
  from I2C capacitive (FT6336G) to SPI resistive (XPT2046). Everything
  else (solar-calculated candle-lighting/havdalah times, the settings
  menu, fonts, the ShabbatCon color scheme, background NTP retry) is
  identical to the ES3C28P version and unaffected by this hardware swap.

  Typical pins for the classic CYD (Sunton ESP32-2432S028R):
    MCU:          plain ESP32 (WROOM-32), NOT ESP32-S3 — select
                  "ESP32 Dev Module" as the board, not "ESP32S3 Dev Module".
    LCD driver:   ILI9341, SPI
                  CS=15  DC=2  SCLK=14  MOSI=13  MISO=12  BL=21 (PWM-dimmable)
                  Reset is tied to EN, no separate GPIO — pass -1.
    Touch driver: XPT2046, resistive, SPI (shares the display's SCLK/MOSI/MISO)
                  CS=33  IRQ=36
    USB:          CH340 or CP2102 USB-UART bridge — plain Serial works
                  normally here; this board doesn't have the ESP32-S3
                  native-USB quirk the other version had to work around.

  IMPORTANT — CYD variants: "CYD" isn't one board, it's a whole family from
  Sunton/JYE and others. This sketch targets the common resistive-touch
  2.8" ILI9341 variant (board silkscreen often reads "ESP32-2432S028R").
  If yours has capacitive touch instead (GT911 or CST820, often silkscreened
  "...028C" or "...028CN"), only the touch-driver section below needs
  swapping for an I2C one — everything else on this page still applies.
  Resistive-touch calibration also varies unit to unit; see the
  TOUCH_DEBUG toggle near the touch driver code below if taps land
  off-target.

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
  2. Libraries (all available via Arduino Library Manager / Cloud Editor's
     Libraries panel): Adafruit GFX Library, Adafruit ILI9341,
     XPT2046_Touchscreen (by Paul Stoffregen), ArduinoJson.
     WiFi, HTTPClient, Preferences, SPI ship with the ESP32 core.
  3. Board: "ESP32 Dev Module" (NOT ESP32S3). Flash size 4MB, default
     partition scheme.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <time.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
// Rounder sans-serif fonts (bundled with Adafruit_GFX) used for all chrome
// text — menus, buttons, captions, keypad. The seven-segment countdown
// digits are hand-drawn from rectangles (drawSevenSegDigit), not text, so
// they're unaffected by font choice and keep their blocky LED look.
#include <math.h>

// These are declared this early (right after the includes) because the
// Arduino build system auto-generates function prototypes and inserts them
// immediately after the last #include — before any type defined later in
// this file would otherwise be visible. Several functions take these enums
// as parameters, so they have to exist before that insertion point.
enum AppScreen { SCR_CLOCK, SCR_SETTINGS, SCR_KEYPAD, SCR_BRIGHTNESS, SCR_TEXTPAD };
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
const int DEFAULT_BRIGHTNESS_PCT = 100;
// ======================================

// ---------- CYD (Sunton ESP32-2432S028R) pins ----------
#define LCD_CS   15
#define LCD_DC    2
#define LCD_RST  -1   // tied to EN, no separate GPIO
#define LCD_SCLK 14
#define LCD_MOSI 13
#define LCD_MISO 12
#define LCD_BL   21

#define TOUCH_CS  33
#define TOUCH_IRQ 36

#define BL_PWM_CHANNEL 0
#define BL_PWM_FREQ    5000
#define BL_PWM_RES     8
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
#define COLOR_DEL        0xB000  // muted red — DEL key accent, unchanged
// -----------------------------------------------------------

SPIClass tftSPI(HSPI);
Adafruit_ILI9341 tft = Adafruit_ILI9341(&tftSPI, LCD_DC, LCD_CS, LCD_RST);
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

// ---------------- Runtime state ----------------
time_t candleLightingEpoch = 0;
time_t havdalahEpoch = 0;
bool needsGeocode = false;
unsigned long lastDisplayUpdate = 0;
bool screenNeedsFullRedraw = true;

// True once NTP has ever successfully set the system clock this session.
// Nothing that depends on wall-clock time (geocode kickoff, Shabbat-time
// math) runs until this is true, since without it time(nullptr) is
// meaningless. Wi-Fi/NTP is retried in the background from loop() — see
// attemptNtpSync() — so this flips true automatically whenever a
// connection becomes available, with no reboot required.
bool hasWallClock = false;
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

// ---------------- Touch driver: XPT2046 (resistive, shares display SPI bus) ----------------
// Raw ADC range the touch controller reports at each axis extreme, mapped
// onto our 320x240 landscape screen. These are typical starting values for
// this board family, but resistive panels vary unit to unit and by
// rotation — if taps land off-target, flip TOUCH_DEBUG to 1 below, tap a
// few known points, and adjust these four numbers (and/or swap p.x/p.y
// below, or flip a map() range) until it tracks correctly. Same empirical
// approach used to derive the capacitive board's touch mapping earlier in
// this project — just done once, on your own unit, instead of guessed here.
#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800

// Set to 1 to print raw touch ADC coordinates in the corner of the screen
// on every tap, for recalibrating the four constants above. Leave at 0
// for normal use.
#define TOUCH_DEBUG 1

bool touchInit() {
  ts.begin(tftSPI); // shares the display's SPI bus; touch has its own CS/IRQ pins
  return true;
}

bool getTouchPoint(int16_t &x, int16_t &y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();

#if TOUCH_DEBUG
  tft.fillRect(0, 231, 140, 9, COLOR_BG);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_INFO, COLOR_BG);
  tft.setCursor(0, 232);
  tft.print(p.x); tft.print(","); tft.print(p.y);
#endif

  x = map(p.x, TS_MINX, TS_MAXX, 0, 319);
  y = map(p.y, TS_MINY, TS_MAXY, 0, 239);
  x = constrain(x, 0, 319);
  y = constrain(y, 0, 239);
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
    drawCandleIcon(148, 100);
    drawCandleIcon(168, 100);
    printCentered("CONNECTING TO WI-FI", 160, 130, 2, COLOR_ACCENT_DIM, COLOR_BG);
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
    time_t fridaySunset = sunsetEpochFor(fy, fm, fd, latitude, longitude);

    int sy, sm, sd;
    civilFromDays(fridayDays + 1, sy, sm, sd);
    time_t satSunset = sunsetEpochFor(sy, sm, sd, latitude, longitude);

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

void doGeocode() {
  tft.fillRect(0, 30, 320, 190, COLOR_BG);
  printCentered("LOCATING...", 160, 105, 2, COLOR_ACCENT_DIM, COLOR_BG);
  if (geocodeZip(currentZip)) {
    saveCoords(latitude, longitude);
    needsGeocode = false;
    if (hasWallClock) {
      if (!hasUtcOffset) {
        localUtcOffsetSeconds = (long)lround(longitude / 15.0) * 3600; // rough fallback
        saveUtcOffset();
      }
      computeShabbatTimes();
    }
    // If we don't have a wall clock yet, leave it here — loop()'s
    // background NTP retry will call computeShabbatTimes() itself the
    // moment the clock syncs, now that hasCoords is true.
  } else {
    printCentered("ZIP LOOKUP FAILED", 160, 100, 2, 0xF800, COLOR_BG);
    printCentered("Check ZIP in Settings & retry", 160, 132, 1, COLOR_ACCENT_DIM, COLOR_BG);
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
  prefs.end();
}

void saveZip(const String &zip) {
  prefs.begin("shabbat", false);
  prefs.putString("zip", zip);
  prefs.end();
  currentZip = zip;
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

void saveWifiCreds(const String &ssid, const String &pass) {
  prefs.begin("shabbat", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  currentSsid = ssid;
  currentPass = pass;
  hasWifiCreds = currentSsid.length() > 0 && currentSsid != "YOUR_WIFI_SSID";
  // Try connecting right away instead of waiting for the next background
  // retry, so entering credentials gives immediate feedback.
  hasWallClock = attemptNtpSync(15000, 8000, true);
  lastNtpAttemptMs = millis();
  if (hasWallClock) {
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
#define DIGIT_W 30
#define DIGIT_H 54
#define COLON_W 14
#define SEG_GAP 5
#define CLOCK_Y 82

int digitX[7];
int colonX[3];
int lastDigits[7] = { -1, -1, -1, -1, -1, -1, -1 };

void computeClockLayout() {
  int totalW = 7 * DIGIT_W + 3 * COLON_W + 9 * SEG_GAP;
  int startX = (320 - totalW) / 2;
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
void drawStaticUI() {
  tft.fillScreen(COLOR_BG);

  tft.fillRect(0, 0, 320, 30, COLOR_PANEL);
  tft.drawFastHLine(0, 30, 320, COLOR_ACCENT);

  drawCandleIcon(8, 14);
  drawCandleIcon(18, 14);
  String zipText = "ZIP " + currentZip;
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_INFO, COLOR_PANEL);
  int16_t zbx, zby; uint16_t zbw, zbh;
  tft.getTextBounds(zipText.c_str(), 0, 0, &zbx, &zby, &zbw, &zbh);
  tft.setCursor(34 - zbx, 9 - zby); // top-left at (34,9), vertically balanced with the candle icons
  tft.print(zipText);
  tft.setFont(); // reset to built-in font

  // Wrench settings button, top-right (no visible box; the tap target in
  // loop() is still the same 286-312 x 2-28 region the box used to outline)
  drawGearIcon(299, 15, 7);

  for (int i = 0; i < 7; i++) lastDigits[i] = -1;
  screenNeedsFullRedraw = true;
}

void drawCountdownFrame() {
  tft.fillRect(0, 30, 320, 190, COLOR_BG);

  if (!hasWifiCreds) {
    printCentered("WI-FI NOT SET UP", 160, 95, 2, COLOR_ACCENT_DIM, COLOR_BG);
    printCentered("(tap the wrench to configure)", 160, 125, 1, COLOR_ACCENT_DIM, COLOR_BG);
    return;
  }
  if (!hasWallClock) {
    printCentered("WAITING FOR WI-FI", 160, 95, 2, COLOR_ACCENT_DIM, COLOR_BG);
    printCentered("(clock not set yet)", 160, 125, 1, COLOR_ACCENT_DIM, COLOR_BG);
    return;
  }
  if (needsGeocode) {
    printCentered("LOCATING...", 160, 105, 2, COLOR_ACCENT_DIM, COLOR_BG);
    return;
  }
  if (candleLightingEpoch == 0 || havdalahEpoch == 0) {
    printCentered("CALCULATING...", 160, 105, 2, COLOR_ACCENT_DIM, COLOR_BG);
    return;
  }

  printCentered("TIME REMAINING", 160, 40, 3, COLOR_ACCENT, COLOR_BG);

  int fx0 = digitX[0] - 14, fy0 = CLOCK_Y - 12;
  int fx1 = digitX[6] + DIGIT_W + 14, fy1 = CLOCK_Y + DIGIT_H + 22;
  tft.drawRoundRect(fx0, fy0, fx1 - fx0, fy1 - fy0, 8, COLOR_PANEL_EDGE);

  String candleLine = "CANDLES " + formatLocalTime(candleLightingEpoch);
  String havdalahLine = "HAVDALAH " + formatLocalTime(havdalahEpoch);

  // Two lines, center-justified, evenly distributed across the remaining
  // space below the clock frame down to the bottom of the content area.
  int areaTop = fy1 + 8;     // just under the frame
  int areaBottom = 216;      // near the bottom of the drawable content region
  int half = (areaBottom - areaTop) / 2;
  int line1Y = areaTop + half / 2 - 8;          // -8: half the size-2 text height
  int line2Y = areaTop + half + half / 2 - 8;

  printCentered(candleLine.c_str(), 160, line1Y, 2, COLOR_INFO, COLOR_BG);
  printCentered(havdalahLine.c_str(), 160, line2Y, 2, COLOR_INFO, COLOR_BG);

  tft.setTextSize(1);
  tft.setTextColor(COLOR_ACCENT_DIM, COLOR_BG);
  tft.setCursor(digitX[0] + 6, CLOCK_Y + DIGIT_H + 8);
  tft.print("D");
  tft.setCursor(digitX[1] + 6, CLOCK_Y + DIGIT_H + 8);
  tft.print("HH");
  tft.setCursor(digitX[3] + 6, CLOCK_Y + DIGIT_H + 8);
  tft.print("MM");
  tft.setCursor(digitX[5] + 6, CLOCK_Y + DIGIT_H + 8);
  tft.print("SS");

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
// Screen drawing: settings menu
// =========================================================================
void drawSettingsMenu() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, 320, 26, COLOR_PANEL);
  tft.drawFastHLine(0, 26, 320, COLOR_ACCENT);
  printCentered("SETTINGS", 160, 5, 1, COLOR_INFO, COLOR_PANEL);

  // Six rows now (Wi-Fi added) — a bit shorter/tighter than the original
  // five-row layout so they still all fit within the 240px screen height.
  const char* labels[6] = { "ZIP CODE", "WI-FI", "BRIGHTNESS", "SET TIME", "HAVDALAH OFFSET", "BACK" };
  for (int i = 0; i < 6; i++) {
    int y = 30 + i * 34;
    bool isBack = (i == 5);
    tft.fillRoundRect(20, y, 280, 30, 8, COLOR_PANEL);
    tft.drawRoundRect(20, y, 280, 30, 8, isBack ? COLOR_ACCENT : COLOR_PANEL_EDGE);
    printCentered(labels[i], 160, y + 8, 2, isBack ? COLOR_ACCENT : COLOR_INFO, COLOR_PANEL);
  }
}

int settingsMenuHit(int16_t x, int16_t y) {
  if (x < 20 || x > 300) return -1;
  for (int i = 0; i < 6; i++) {
    int ry = 30 + i * 34;
    if (y >= ry && y <= ry + 30) return i;
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
    case 1: startTextEntry(TXT_SSID, 32, currentSsid); break;
    case 2: screen = SCR_BRIGHTNESS; drawBrightnessScreen(); break;
    case 3: startKeypad(KP_YEAR, 4); break;
    case 4: startKeypad(KP_HAVDALAH, 3); break;
    case 5: screen = SCR_CLOCK; drawStaticUI(); break;
  }
}

// =========================================================================
// Screen drawing: brightness
// =========================================================================
void drawBrightnessScreen() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, 320, 26, COLOR_PANEL);
  tft.drawFastHLine(0, 26, 320, COLOR_ACCENT);
  printCentered("BRIGHTNESS", 160, 5, 1, COLOR_INFO, COLOR_PANEL);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", brightnessPct);
  printCentered(buf, 160, 80, 4, COLOR_ACCENT, COLOR_BG);

  tft.fillRoundRect(40, 150, 80, 50, 10, COLOR_PANEL);
  tft.drawRoundRect(40, 150, 80, 50, 10, COLOR_PANEL_EDGE);
  printCentered("-", 80, 160, 3, COLOR_INFO, COLOR_PANEL);

  tft.fillRoundRect(200, 150, 80, 50, 10, COLOR_PANEL);
  tft.drawRoundRect(200, 150, 80, 50, 10, COLOR_PANEL_EDGE);
  printCentered("+", 240, 160, 3, COLOR_INFO, COLOR_PANEL);

  tft.fillRoundRect(110, 210, 100, 26, 8, COLOR_ACCENT);
  printCentered("DONE", 160, 215, 1, COLOR_BG, COLOR_ACCENT);
}

void handleBrightnessTouch(int16_t x, int16_t y) {
  if (x >= 40 && x <= 120 && y >= 150 && y <= 200) {
    brightnessPct = max(10, brightnessPct - 10);
    applyBrightness(); saveBrightness(); drawBrightnessScreen();
  } else if (x >= 200 && x <= 280 && y >= 150 && y <= 200) {
    brightnessPct = min(100, brightnessPct + 10);
    applyBrightness(); saveBrightness(); drawBrightnessScreen();
  } else if (x >= 110 && x <= 210 && y >= 210 && y <= 236) {
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

void drawKeypad() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, 320, 26, COLOR_PANEL);
  tft.drawFastHLine(0, 26, 320, COLOR_ACCENT);
  printCentered(keypadTitle(), 160, 5, 1, COLOR_INFO, COLOR_PANEL);

  int n = keypadDigits;
  int boxW = 34, boxGap = 8;
  int startX = 160 - (n * boxW + (n - 1) * boxGap) / 2;
  for (int i = 0; i < n; i++) {
    int bx = startX + i * (boxW + boxGap);
    bool filled = i < keypadBuffer.length();
    tft.drawRoundRect(bx, 32, boxW, 28, 5, filled ? COLOR_ACCENT : COLOR_PANEL_EDGE);
    if (filled) {
      char c[2] = { keypadBuffer[i], 0 };
      printCentered(c, bx + boxW / 2, 39, 2, COLOR_INFO, COLOR_BG);
    }
  }

  const char* keys[12] = {"1","2","3","4","5","6","7","8","9","DEL","0","OK"};
  int idx = 0;
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 3; col++) {
      int x = 10 + col * 105;
      int y = 68 + row * 40;
      uint16_t keyColor = COLOR_INFO;
      uint16_t borderColor = COLOR_PANEL_EDGE;
      if (keys[idx][0] == 'D') { keyColor = COLOR_DEL; borderColor = COLOR_DEL; }

      if (keys[idx][0] == 'O') {
        tft.fillRoundRect(x, y, 95, 34, 8, COLOR_ACCENT);
        printCentered(keys[idx], x + 47, y + 10, 2, COLOR_BG, COLOR_ACCENT);
      } else {
        tft.fillRoundRect(x, y, 95, 34, 8, COLOR_PANEL);
        tft.drawRoundRect(x, y, 95, 34, 8, borderColor);
        printCentered(keys[idx], x + 47, y + 10, 2, keyColor, COLOR_PANEL);
      }
      idx++;
    }
  }
}

int keypadHit(int16_t x, int16_t y) {
  if (y < 68 || y > 68 + 4 * 40) return -1;
  int row = (y - 68) / 40;
  int col = (x - 10) / 105;
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
        time_t enteredLocalEpoch = utcToEpoch(tmpYear, tmpMonth, tmpDay, tmpHour, tmpMinute, 0);
        time_t nowUtc = time(nullptr);
        localUtcOffsetSeconds = (long)(enteredLocalEpoch - nowUtc);
        saveUtcOffset();
        if (hasCoords) computeShabbatTimes();
        screen = SCR_SETTINGS; drawSettingsMenu();
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
  tft.fillRect(0, 30, 320, 30, COLOR_BG);
  tft.drawRoundRect(4, 32, 312, 26, 5, COLOR_PANEL_EDGE);
  String shown = textBuffer;
  const int maxChars = 30; // roughly what fits at size-1 in the field width
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

void drawTextPad() {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(0, 0, 320, 26, COLOR_PANEL);
  tft.drawFastHLine(0, 26, 320, COLOR_ACCENT);
  printCentered(textPurpose == TXT_SSID ? "WI-FI NETWORK NAME" : "WI-FI PASSWORD",
                160, 5, 1, COLOR_INFO, COLOR_PANEL);

  drawTextField();

  const int kbTop = 66, rowStep = 40, keyH = 34;
  const char* row0 = textRow0();
  const char* row1 = textRow1();
  const char* row2 = textRow2();

  // Row 0: 10 keys, full width
  int w0 = 32;
  for (int i = 0; i < 10; i++) {
    char c[2] = { row0[i], 0 };
    int x = i * w0;
    tft.fillRoundRect(x + 1, kbTop, w0 - 2, keyH, 4, COLOR_PANEL);
    tft.drawRoundRect(x + 1, kbTop, w0 - 2, keyH, 4, COLOR_PANEL_EDGE);
    printCentered(c, x + w0 / 2, kbTop + 9, 2, COLOR_INFO, COLOR_PANEL);
  }

  // Row 1: 9 keys
  int y1 = kbTop + rowStep;
  int w1 = 320 / 9;
  for (int i = 0; i < 9; i++) {
    char c[2] = { row1[i], 0 };
    int x = i * w1;
    tft.fillRoundRect(x + 1, y1, w1 - 2, keyH, 4, COLOR_PANEL);
    tft.drawRoundRect(x + 1, y1, w1 - 2, keyH, 4, COLOR_PANEL_EDGE);
    printCentered(c, x + w1 / 2, y1 + 9, 2, COLOR_INFO, COLOR_PANEL);
  }

  // Row 2: MODE key + 7 keys + DEL
  int y2 = kbTop + 2 * rowStep;
  const char* modeLabel = (textMode == TXT_LOWER) ? "ABC" : (textMode == TXT_UPPER ? "123" : "abc");
  tft.fillRoundRect(1, y2, 46, keyH, 4, COLOR_PANEL);
  tft.drawRoundRect(1, y2, 46, keyH, 4, COLOR_ACCENT);
  printCentered(modeLabel, 24, y2 + 9, 1, COLOR_ACCENT, COLOR_PANEL);

  int w2 = 32;
  for (int i = 0; i < 7; i++) {
    char c[2] = { row2[i], 0 };
    int x = 48 + i * w2;
    tft.fillRoundRect(x + 1, y2, w2 - 2, keyH, 4, COLOR_PANEL);
    tft.drawRoundRect(x + 1, y2, w2 - 2, keyH, 4, COLOR_PANEL_EDGE);
    printCentered(c, x + w2 / 2, y2 + 9, 2, COLOR_INFO, COLOR_PANEL);
  }

  tft.fillRoundRect(273, y2, 46, keyH, 4, COLOR_PANEL);
  tft.drawRoundRect(273, y2, 46, keyH, 4, COLOR_DEL);
  printCentered("DEL", 296, y2 + 9, 1, COLOR_DEL, COLOR_PANEL);

  // Row 3: SPACE + OK
  int y3 = kbTop + 3 * rowStep;
  tft.fillRoundRect(1, y3, 248, keyH, 4, COLOR_PANEL);
  tft.drawRoundRect(1, y3, 248, keyH, 4, COLOR_PANEL_EDGE);
  printCentered("SPACE", 125, y3 + 9, 1, COLOR_INFO, COLOR_PANEL);

  tft.fillRoundRect(250, y3, 69, keyH, 4, COLOR_ACCENT);
  printCentered("OK", 284, y3 + 9, 1, COLOR_BG, COLOR_ACCENT);
}

// Returns a key code: 0-9 (row 0), 100-108 (row 1), 200-206 (row 2 middle),
// 300 (MODE), 301 (DEL), 302 (SPACE), 303 (OK), or -1 for no hit.
int textPadHit(int16_t x, int16_t y) {
  const int kbTop = 66, rowStep = 40;
  if (y < kbTop || y > kbTop + 4 * rowStep) return -1;
  int row = (y - kbTop) / rowStep;

  if (row == 0) {
    int col = x / 32;
    if (col < 0 || col > 9) return -1;
    return col;
  }
  if (row == 1) {
    int col = x / (320 / 9);
    if (col < 0 || col > 8) return -1;
    return 100 + col;
  }
  if (row == 2) {
    if (x < 48) return 300;
    if (x >= 273) return 301;
    int col = (x - 48) / 32;
    if (col < 0 || col > 6) return -1;
    return 200 + col;
  }
  if (row == 3) {
    return (x < 250) ? 302 : 303;
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
      screen = SCR_SETTINGS;
      drawSettingsMenu();
    }
  }
}

// =========================================================================
// Setup / Loop
// =========================================================================
void setup() {
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

  // Bounded attempt at boot — if no network shows up within the timeout,
  // we carry on without one instead of hanging here forever. loop() keeps
  // retrying quietly in the background so a later connection still syncs.
  hasWallClock = attemptNtpSync(15000, 8000, true);
  lastNtpAttemptMs = millis();

  if (hasWallClock) {
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

  switch (screen) {
    case SCR_CLOCK:
      if (pressed && x >= 286 && x <= 312 && y >= 2 && y <= 28) {
        screen = SCR_SETTINGS;
        drawSettingsMenu();
        break;
      }
      // Background NTP: retry often until we've ever synced, then just
      // occasionally to correct drift. Each attempt is bounded and silent
      // (no "CONNECTING..." screen) so a missing network only stalls the
      // touchscreen briefly and rarely, rather than blocking forever.
      {
        unsigned long retryInterval = hasWallClock ? NTP_RESYNC_INTERVAL_MS : NTP_RETRY_INTERVAL_MS;
        if (millis() - lastNtpAttemptMs > retryInterval) {
          lastNtpAttemptMs = millis();
          bool hadWallClock = hasWallClock;
          if (attemptNtpSync(6000, 5000, false)) {
            hasWallClock = true;
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
        if (screenNeedsFullRedraw) {
          drawCountdownFrame();
          screenNeedsFullRedraw = false;
        }
        updateCountdown();
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
  }
}
