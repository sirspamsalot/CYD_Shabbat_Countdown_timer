// Auto-generated list of 250 major world cities with approximate
// lat/lon (city-center precision -- plenty for solar sunset-time
// calculation, which only needs to be accurate to within a fraction
// of a degree). Lets a user pick a city instead of typing a ZIP code,
// which matters outside the US since the ZIP->lat/lon lookup
// (api.zippopotam.us) only covers US ZIP codes.
//
// No PROGMEM/pgm_read_* needed here: unlike AVR, the ESP32's flash is
// memory-mapped, so a plain "const" array already lives in flash and
// is directly readable -- unlike the classic Arduino-Uno-era pattern
// you may have seen elsewhere.
//
// ---------------------------------------------------------------------
// utcOffsetHours / dstRule: added so the FULLY-OFFLINE "SET TIME + CITY"
// path (see shabbat_countdown_epaper.ino's clockIsLocalLabeled /
// solarEpochShiftFor()) can line up its astronomical (always true-UTC)
// sunset calculation with the device's manually-set clock EXACTLY,
// instead of guessing from longitude + hemisphere (which is wrong for
// half-hour zones like India, single-zone-despite-huge-longitude-span
// countries like China, places that don't observe DST at all like
// Arizona or Japan, and countries whose DST dates differ from the
// generic guess). This only matters for that fully-offline path — the
// online Wi-Fi/NTP path always derives a real timezone from a lookup
// and ignores these two fields entirely.
//
//   utcOffsetHours: the location's STANDARD (non-DST / "winter") UTC
//                   offset, e.g. -5.0 for US Eastern, 5.5 for India,
//                   5.75 for Nepal. This is NOT what's in effect during
//                   DST — dstRule below is what adds the extra hour.
//   dstRule:        which, if any, daylight-saving schedule applies —
//                   one of the CITY_DST_* constants below. CITY_DST_NONE
//                   means standard time is used year-round.
//
// These values are a best-effort snapshot of each country's DST policy
// as of this sketch's writing and WILL drift out of date — countries
// change DST rules more often than you'd expect (a few recent examples:
// Mexico dropped DST nationally in 2022 except its US border strip,
// Jordan dropped it in 2022, Turkey in 2016, Egypt reinstated it in
// 2023 with its own dates, Paraguay suspended it around 2024). Entries
// for countries with unusually volatile or unusual DST policy (Egypt,
// Morocco, Syria, Chile, Paraguay) are flagged inline with a comment —
// treat those as the least trustworthy entries in the table, and check
// current local rules if you depend on one of them. Morocco's own
// practice of reverting to UTC+0 for the duration of Ramadan each year
// is NOT modeled here at all (it would need a lunar calendar), so
// Casablanca/Rabat will be about an hour off during that month.
//
// The five DST schedules modeled (see dstRuleActive() in the .ino):
//   CITY_DST_US: 2nd Sunday in March -> 1st Sunday in November (US/Canada)
//   CITY_DST_EU: last Sunday in March -> last Sunday in October (EU and
//                aligned countries, e.g. UK, Switzerland)
//   CITY_DST_AU: 1st Sunday in October -> 1st Sunday in April (most of
//                Australia; also used as a rough stand-in for other
//                Southern-Hemisphere DST countries whose exact dates
//                differ, e.g. Chile)
//   CITY_DST_NZ: last Sunday in September -> 1st Sunday in April
//                (New Zealand)
//   CITY_DST_IL: the Friday before the last Sunday in March -> the last
//                Sunday in October (Israel) — called out as its own
//                rule since it doesn't match any of the others and this
//                is, after all, a Shabbat clock.
// ---------------------------------------------------------------------
#pragma once

#define CITY_DST_NONE 0
#define CITY_DST_US   1
#define CITY_DST_EU   2
#define CITY_DST_AU   3
#define CITY_DST_NZ   4
#define CITY_DST_IL   5

struct CityEntry {
  const char* name;        // "City, Country"
  float lat;
  float lon;
  float utcOffsetHours;    // STANDARD (non-DST) UTC offset, e.g. -5.0, 5.5
  uint8_t dstRule;         // one of the CITY_DST_* constants above
};

const int CITY_COUNT = 250;
const CityEntry CITY_LIST[CITY_COUNT] = {
  {"Abidjan, Ivory Coast", 5.36f, -4.01f, 0.0f, CITY_DST_NONE},
  {"Abu Dhabi, United Arab Emirates", 24.45f, 54.38f, 4.0f, CITY_DST_NONE},
  {"Accra, Ghana", 5.6f, -0.19f, 0.0f, CITY_DST_NONE},
  {"Addis Ababa, Ethiopia", 9.03f, 38.75f, 3.0f, CITY_DST_NONE},
  {"Adelaide, Australia", -34.93f, 138.6f, 9.5f, CITY_DST_AU},
  {"Ahmedabad, India", 23.02f, 72.57f, 5.5f, CITY_DST_NONE},
  {"Alexandria, Egypt", 31.2f, 29.92f, 2.0f, CITY_DST_EU},  // DST policy uncertain/volatile
  {"Algiers, Algeria", 36.75f, 3.06f, 1.0f, CITY_DST_NONE},
  {"Almaty, Kazakhstan", 43.24f, 76.9f, 6.0f, CITY_DST_NONE},
  {"Amman, Jordan", 31.95f, 35.93f, 3.0f, CITY_DST_NONE},
  {"Amsterdam, Netherlands", 52.37f, 4.9f, 1.0f, CITY_DST_EU},
  {"Ankara, Turkey", 39.93f, 32.86f, 3.0f, CITY_DST_NONE},
  {"Antananarivo, Madagascar", -18.88f, 47.51f, 3.0f, CITY_DST_NONE},
  {"Antwerp, Belgium", 51.22f, 4.4f, 1.0f, CITY_DST_EU},
  {"Ashgabat, Turkmenistan", 37.96f, 58.33f, 5.0f, CITY_DST_NONE},
  {"Astana, Kazakhstan", 51.17f, 71.42f, 5.0f, CITY_DST_NONE},
  {"Asuncion, Paraguay", -25.3f, -57.64f, -4.0f, CITY_DST_NONE},  // DST policy uncertain/volatile
  {"Athens, Greece", 37.98f, 23.73f, 2.0f, CITY_DST_EU},
  {"Atlanta, USA", 33.75f, -84.39f, -5.0f, CITY_DST_US},
  {"Auckland, New Zealand", -36.85f, 174.76f, 12.0f, CITY_DST_NZ},
  {"Austin, USA", 30.27f, -97.74f, -6.0f, CITY_DST_US},
  {"Baghdad, Iraq", 33.31f, 44.36f, 3.0f, CITY_DST_NONE},
  {"Baku, Azerbaijan", 40.41f, 49.87f, 4.0f, CITY_DST_NONE},
  {"Baltimore, USA", 39.29f, -76.61f, -5.0f, CITY_DST_US},
  {"Bangalore, India", 12.97f, 77.59f, 5.5f, CITY_DST_NONE},
  {"Bangkok, Thailand", 13.76f, 100.5f, 7.0f, CITY_DST_NONE},
  {"Barcelona, Spain", 41.39f, 2.17f, 1.0f, CITY_DST_EU},
  {"Beijing, China", 39.9f, 116.41f, 8.0f, CITY_DST_NONE},
  {"Beirut, Lebanon", 33.89f, 35.5f, 2.0f, CITY_DST_EU},
  {"Belgrade, Serbia", 44.79f, 20.45f, 1.0f, CITY_DST_EU},
  {"Belo Horizonte, Brazil", -19.92f, -43.94f, -3.0f, CITY_DST_NONE},
  {"Bergen, Norway", 60.39f, 5.32f, 1.0f, CITY_DST_EU},
  {"Berlin, Germany", 52.52f, 13.4f, 1.0f, CITY_DST_EU},
  {"Birmingham, United Kingdom", 52.49f, -1.89f, 0.0f, CITY_DST_EU},
  {"Bishkek, Kyrgyzstan", 42.87f, 74.59f, 6.0f, CITY_DST_NONE},
  {"Bogota, Colombia", 4.71f, -74.07f, -5.0f, CITY_DST_NONE},
  {"Boston, USA", 42.36f, -71.06f, -5.0f, CITY_DST_US},
  {"Brasilia, Brazil", -15.79f, -47.88f, -3.0f, CITY_DST_NONE},
  {"Bratislava, Slovakia", 48.15f, 17.11f, 1.0f, CITY_DST_EU},
  {"Brisbane, Australia", -27.47f, 153.03f, 10.0f, CITY_DST_NONE},
  {"Brussels, Belgium", 50.85f, 4.35f, 1.0f, CITY_DST_EU},
  {"Bucharest, Romania", 44.43f, 26.1f, 2.0f, CITY_DST_EU},
  {"Budapest, Hungary", 47.5f, 19.04f, 1.0f, CITY_DST_EU},
  {"Buenos Aires, Argentina", -34.6f, -58.38f, -3.0f, CITY_DST_NONE},
  {"Busan, South Korea", 35.18f, 129.08f, 9.0f, CITY_DST_NONE},
  {"Cairo, Egypt", 30.04f, 31.24f, 2.0f, CITY_DST_EU},  // DST policy uncertain/volatile
  {"Calgary, Canada", 51.05f, -114.07f, -7.0f, CITY_DST_US},
  {"Canberra, Australia", -35.28f, 149.13f, 10.0f, CITY_DST_AU},
  {"Cape Town, South Africa", -33.92f, 18.42f, 2.0f, CITY_DST_NONE},
  {"Caracas, Venezuela", 10.49f, -66.88f, -4.0f, CITY_DST_NONE},
  {"Casablanca, Morocco", 33.57f, -7.59f, 1.0f, CITY_DST_NONE},  // DST policy uncertain/volatile
  {"Cebu City, Philippines", 10.32f, 123.9f, 8.0f, CITY_DST_NONE},
  {"Charlotte, USA", 35.23f, -80.84f, -5.0f, CITY_DST_US},
  {"Chengdu, China", 30.57f, 104.07f, 8.0f, CITY_DST_NONE},
  {"Chennai, India", 13.08f, 80.27f, 5.5f, CITY_DST_NONE},
  {"Chicago, USA", 41.88f, -87.63f, -6.0f, CITY_DST_US},
  {"Chittagong, Bangladesh", 22.36f, 91.78f, 6.0f, CITY_DST_NONE},
  {"Chongqing, China", 29.56f, 106.55f, 8.0f, CITY_DST_NONE},
  {"Christchurch, New Zealand", -43.53f, 172.64f, 12.0f, CITY_DST_NZ},
  {"Cleveland, USA", 41.5f, -81.69f, -5.0f, CITY_DST_US},
  {"Cologne, Germany", 50.94f, 6.96f, 1.0f, CITY_DST_EU},
  {"Colombo, Sri Lanka", 6.93f, 79.85f, 5.5f, CITY_DST_NONE},
  {"Columbus, USA", 39.96f, -83.0f, -5.0f, CITY_DST_US},
  {"Copenhagen, Denmark", 55.68f, 12.57f, 1.0f, CITY_DST_EU},
  {"Dakar, Senegal", 14.72f, -17.47f, 0.0f, CITY_DST_NONE},
  {"Dallas, USA", 32.78f, -96.8f, -6.0f, CITY_DST_US},
  {"Damascus, Syria", 33.51f, 36.29f, 3.0f, CITY_DST_NONE},  // DST policy uncertain/volatile
  {"Dar es Salaam, Tanzania", -6.79f, 39.21f, 3.0f, CITY_DST_NONE},
  {"Delhi, India", 28.7f, 77.1f, 5.5f, CITY_DST_NONE},
  {"Denver, USA", 39.74f, -104.99f, -7.0f, CITY_DST_US},
  {"Detroit, USA", 42.33f, -83.05f, -5.0f, CITY_DST_US},
  {"Dhaka, Bangladesh", 23.81f, 90.41f, 6.0f, CITY_DST_NONE},
  {"Doha, Qatar", 25.29f, 51.53f, 3.0f, CITY_DST_NONE},
  {"Dubai, United Arab Emirates", 25.2f, 55.27f, 4.0f, CITY_DST_NONE},
  {"Dublin, Ireland", 53.35f, -6.26f, 0.0f, CITY_DST_EU},
  {"Durban, South Africa", -29.86f, 31.02f, 2.0f, CITY_DST_NONE},
  {"Dushanbe, Tajikistan", 38.56f, 68.79f, 5.0f, CITY_DST_NONE},
  {"Edinburgh, United Kingdom", 55.95f, -3.19f, 0.0f, CITY_DST_EU},
  {"Edmonton, Canada", 53.55f, -113.49f, -7.0f, CITY_DST_US},
  {"Fortaleza, Brazil", -3.72f, -38.54f, -3.0f, CITY_DST_NONE},
  {"Frankfurt, Germany", 50.11f, 8.68f, 1.0f, CITY_DST_EU},
  {"Fukuoka, Japan", 33.59f, 130.4f, 9.0f, CITY_DST_NONE},
  {"Geneva, Switzerland", 46.2f, 6.14f, 1.0f, CITY_DST_EU},
  {"Glasgow, United Kingdom", 55.86f, -4.25f, 0.0f, CITY_DST_EU},
  {"Guadalajara, Mexico", 20.66f, -103.35f, -6.0f, CITY_DST_NONE},
  {"Guangzhou, China", 23.13f, 113.26f, 8.0f, CITY_DST_NONE},
  {"Guatemala City, Guatemala", 14.63f, -90.51f, -6.0f, CITY_DST_NONE},
  {"Guayaquil, Ecuador", -2.19f, -79.89f, -5.0f, CITY_DST_NONE},
  {"Haifa, Israel", 32.79f, 34.99f, 2.0f, CITY_DST_IL},
  {"Hamburg, Germany", 53.55f, 9.99f, 1.0f, CITY_DST_EU},
  {"Hanoi, Vietnam", 21.03f, 105.85f, 7.0f, CITY_DST_NONE},
  {"Harare, Zimbabwe", -17.83f, 31.05f, 2.0f, CITY_DST_NONE},
  {"Havana, Cuba", 23.13f, -82.38f, -5.0f, CITY_DST_US},
  {"Helsinki, Finland", 60.17f, 24.94f, 2.0f, CITY_DST_EU},
  {"Ho Chi Minh City, Vietnam", 10.78f, 106.66f, 7.0f, CITY_DST_NONE},
  {"Hong Kong, China", 22.32f, 114.17f, 8.0f, CITY_DST_NONE},
  {"Houston, USA", 29.76f, -95.37f, -6.0f, CITY_DST_US},
  {"Hyderabad, India", 17.39f, 78.49f, 5.5f, CITY_DST_NONE},
  {"Indianapolis, USA", 39.77f, -86.16f, -5.0f, CITY_DST_US},
  {"Islamabad, Pakistan", 33.68f, 73.05f, 5.0f, CITY_DST_NONE},
  {"Istanbul, Turkey", 41.01f, 28.98f, 3.0f, CITY_DST_NONE},
  {"Izmir, Turkey", 38.42f, 27.14f, 3.0f, CITY_DST_NONE},
  {"Jacksonville, USA", 30.33f, -81.66f, -5.0f, CITY_DST_US},
  {"Jakarta, Indonesia", -6.21f, 106.85f, 7.0f, CITY_DST_NONE},
  {"Jeddah, Saudi Arabia", 21.49f, 39.19f, 3.0f, CITY_DST_NONE},
  {"Jerusalem, Israel", 31.78f, 35.22f, 2.0f, CITY_DST_IL},
  {"Johannesburg, South Africa", -26.2f, 28.05f, 2.0f, CITY_DST_NONE},
  {"Kabul, Afghanistan", 34.56f, 69.21f, 4.5f, CITY_DST_NONE},
  {"Kampala, Uganda", 0.35f, 32.58f, 3.0f, CITY_DST_NONE},
  {"Kansas City, USA", 39.1f, -94.58f, -6.0f, CITY_DST_US},
  {"Karachi, Pakistan", 24.86f, 67.01f, 5.0f, CITY_DST_NONE},
  {"Kathmandu, Nepal", 27.72f, 85.32f, 5.75f, CITY_DST_NONE},
  {"Khartoum, Sudan", 15.5f, 32.56f, 2.0f, CITY_DST_NONE},
  {"Kingston, Jamaica", 17.97f, -76.79f, -5.0f, CITY_DST_NONE},
  {"Kinshasa, DR Congo", -4.44f, 15.27f, 1.0f, CITY_DST_NONE},
  {"Kolkata, India", 22.57f, 88.36f, 5.5f, CITY_DST_NONE},
  {"Kuala Lumpur, Malaysia", 3.14f, 101.69f, 8.0f, CITY_DST_NONE},
  {"Kuwait City, Kuwait", 29.38f, 47.99f, 3.0f, CITY_DST_NONE},
  {"Kyiv, Ukraine", 50.45f, 30.52f, 2.0f, CITY_DST_EU},
  {"La Paz, Bolivia", -16.5f, -68.15f, -4.0f, CITY_DST_NONE},
  {"Lagos, Nigeria", 6.52f, 3.38f, 1.0f, CITY_DST_NONE},
  {"Lahore, Pakistan", 31.55f, 74.34f, 5.0f, CITY_DST_NONE},
  {"Las Vegas, USA", 36.17f, -115.14f, -8.0f, CITY_DST_US},
  {"Lima, Peru", -12.05f, -77.04f, -5.0f, CITY_DST_NONE},
  {"Lisbon, Portugal", 38.72f, -9.14f, 0.0f, CITY_DST_EU},
  {"Liverpool, United Kingdom", 53.41f, -2.98f, 0.0f, CITY_DST_EU},
  {"Ljubljana, Slovenia", 46.06f, 14.51f, 1.0f, CITY_DST_EU},
  {"London, United Kingdom", 51.51f, -0.13f, 0.0f, CITY_DST_EU},
  {"Los Angeles, USA", 34.05f, -118.24f, -8.0f, CITY_DST_US},
  {"Luanda, Angola", -8.84f, 13.23f, 1.0f, CITY_DST_NONE},
  {"Lusaka, Zambia", -15.39f, 28.32f, 2.0f, CITY_DST_NONE},
  {"Luxembourg, Luxembourg", 49.61f, 6.13f, 1.0f, CITY_DST_EU},
  {"Lyon, France", 45.76f, 4.84f, 1.0f, CITY_DST_EU},
  {"Madrid, Spain", 40.42f, -3.7f, 1.0f, CITY_DST_EU},
  {"Managua, Nicaragua", 12.11f, -86.24f, -6.0f, CITY_DST_NONE},
  {"Manama, Bahrain", 26.23f, 50.59f, 3.0f, CITY_DST_NONE},
  {"Manchester, United Kingdom", 53.48f, -2.24f, 0.0f, CITY_DST_EU},
  {"Manila, Philippines", 14.6f, 120.98f, 8.0f, CITY_DST_NONE},
  {"Maputo, Mozambique", -25.97f, 32.57f, 2.0f, CITY_DST_NONE},
  {"Marseille, France", 43.3f, 5.37f, 1.0f, CITY_DST_EU},
  {"Medellin, Colombia", 6.24f, -75.58f, -5.0f, CITY_DST_NONE},
  {"Melbourne, Australia", -37.81f, 144.96f, 10.0f, CITY_DST_AU},
  {"Mexico City, Mexico", 19.43f, -99.13f, -6.0f, CITY_DST_NONE},
  {"Miami, USA", 25.76f, -80.19f, -5.0f, CITY_DST_US},
  {"Milan, Italy", 45.46f, 9.19f, 1.0f, CITY_DST_EU},
  {"Minneapolis, USA", 44.98f, -93.27f, -6.0f, CITY_DST_US},
  {"Minsk, Belarus", 53.9f, 27.57f, 3.0f, CITY_DST_NONE},
  {"Monterrey, Mexico", 25.69f, -100.32f, -6.0f, CITY_DST_NONE},
  {"Montevideo, Uruguay", -34.9f, -56.16f, -3.0f, CITY_DST_NONE},
  {"Montreal, Canada", 45.5f, -73.57f, -5.0f, CITY_DST_US},
  {"Moscow, Russia", 55.76f, 37.62f, 3.0f, CITY_DST_NONE},
  {"Mumbai, India", 19.08f, 72.88f, 5.5f, CITY_DST_NONE},
  {"Munich, Germany", 48.14f, 11.58f, 1.0f, CITY_DST_EU},
  {"Muscat, Oman", 23.61f, 58.59f, 4.0f, CITY_DST_NONE},
  {"Nagoya, Japan", 35.18f, 136.91f, 9.0f, CITY_DST_NONE},
  {"Nairobi, Kenya", -1.29f, 36.82f, 3.0f, CITY_DST_NONE},
  {"Naples, Italy", 40.85f, 14.27f, 1.0f, CITY_DST_EU},
  {"Nashville, USA", 36.16f, -86.78f, -6.0f, CITY_DST_US},
  {"Naypyidaw, Myanmar", 19.76f, 96.08f, 6.5f, CITY_DST_NONE},
  {"New Orleans, USA", 29.95f, -90.07f, -6.0f, CITY_DST_US},
  {"New York, USA", 40.71f, -74.01f, -5.0f, CITY_DST_US},
  {"Nicosia, Cyprus", 35.17f, 33.36f, 2.0f, CITY_DST_EU},
  {"Orlando, USA", 28.54f, -81.38f, -5.0f, CITY_DST_US},
  {"Osaka, Japan", 34.69f, 135.5f, 9.0f, CITY_DST_NONE},
  {"Oslo, Norway", 59.91f, 10.75f, 1.0f, CITY_DST_EU},
  {"Ottawa, Canada", 45.42f, -75.7f, -5.0f, CITY_DST_US},
  {"Panama City, Panama", 8.98f, -79.52f, -5.0f, CITY_DST_NONE},
  {"Paris, France", 48.86f, 2.35f, 1.0f, CITY_DST_EU},
  {"Perth, Australia", -31.95f, 115.86f, 8.0f, CITY_DST_NONE},
  {"Philadelphia, USA", 39.95f, -75.17f, -5.0f, CITY_DST_US},
  {"Phnom Penh, Cambodia", 11.56f, 104.92f, 7.0f, CITY_DST_NONE},
  {"Phoenix, USA", 33.45f, -112.07f, -7.0f, CITY_DST_NONE},
  {"Podgorica, Montenegro", 42.44f, 19.26f, 1.0f, CITY_DST_EU},
  {"Portland, USA", 45.52f, -122.68f, -8.0f, CITY_DST_US},
  {"Porto, Portugal", 41.15f, -8.61f, 0.0f, CITY_DST_EU},
  {"Prague, Czech Republic", 50.09f, 14.42f, 1.0f, CITY_DST_EU},
  {"Pretoria, South Africa", -25.75f, 28.19f, 2.0f, CITY_DST_NONE},
  {"Puebla, Mexico", 19.04f, -98.2f, -6.0f, CITY_DST_NONE},
  {"Pune, India", 18.52f, 73.86f, 5.5f, CITY_DST_NONE},
  {"Pyongyang, North Korea", 39.02f, 125.75f, 9.0f, CITY_DST_NONE},
  {"Quebec City, Canada", 46.81f, -71.21f, -5.0f, CITY_DST_US},
  {"Quezon City, Philippines", 14.68f, 121.04f, 8.0f, CITY_DST_NONE},
  {"Quito, Ecuador", -0.18f, -78.47f, -5.0f, CITY_DST_NONE},
  {"Rabat, Morocco", 34.02f, -6.83f, 1.0f, CITY_DST_NONE},  // DST policy uncertain/volatile
  {"Reykjavik, Iceland", 64.15f, -21.94f, 0.0f, CITY_DST_NONE},
  {"Riga, Latvia", 56.95f, 24.11f, 2.0f, CITY_DST_EU},
  {"Rio de Janeiro, Brazil", -22.91f, -43.17f, -3.0f, CITY_DST_NONE},
  {"Riyadh, Saudi Arabia", 24.71f, 46.68f, 3.0f, CITY_DST_NONE},
  {"Rome, Italy", 41.9f, 12.5f, 1.0f, CITY_DST_EU},
  {"Rotterdam, Netherlands", 51.92f, 4.48f, 1.0f, CITY_DST_EU},
  {"Sacramento, USA", 38.58f, -121.49f, -8.0f, CITY_DST_US},
  {"Salt Lake City, USA", 40.76f, -111.89f, -7.0f, CITY_DST_US},
  {"Salvador, Brazil", -12.97f, -38.51f, -3.0f, CITY_DST_NONE},
  {"San Antonio, USA", 29.42f, -98.49f, -6.0f, CITY_DST_US},
  {"San Diego, USA", 32.72f, -117.16f, -8.0f, CITY_DST_US},
  {"San Francisco, USA", 37.77f, -122.42f, -8.0f, CITY_DST_US},
  {"San Jose, Costa Rica", 9.93f, -84.08f, -6.0f, CITY_DST_NONE},
  {"San Juan, Puerto Rico", 18.47f, -66.11f, -4.0f, CITY_DST_NONE},
  {"San Salvador, El Salvador", 13.69f, -89.19f, -6.0f, CITY_DST_NONE},
  {"Sanaa, Yemen", 15.37f, 44.19f, 3.0f, CITY_DST_NONE},
  {"Santiago, Chile", -33.45f, -70.65f, -4.0f, CITY_DST_AU},  // DST policy uncertain/volatile
  {"Santo Domingo, Dominican Republic", 18.49f, -69.93f, -4.0f, CITY_DST_NONE},
  {"Sao Paulo, Brazil", -23.55f, -46.63f, -3.0f, CITY_DST_NONE},
  {"Sapporo, Japan", 43.06f, 141.35f, 9.0f, CITY_DST_NONE},
  {"Sarajevo, Bosnia and Herzegovina", 43.86f, 18.41f, 1.0f, CITY_DST_EU},
  {"Seattle, USA", 47.61f, -122.33f, -8.0f, CITY_DST_US},
  {"Seoul, South Korea", 37.57f, 126.98f, 9.0f, CITY_DST_NONE},
  {"Seville, Spain", 37.39f, -5.99f, 1.0f, CITY_DST_EU},
  {"Shanghai, China", 31.23f, 121.47f, 8.0f, CITY_DST_NONE},
  {"Shenzhen, China", 22.54f, 114.06f, 8.0f, CITY_DST_NONE},
  {"Singapore, Singapore", 1.35f, 103.82f, 8.0f, CITY_DST_NONE},
  {"Skopje, North Macedonia", 42.0f, 21.43f, 1.0f, CITY_DST_EU},
  {"Sofia, Bulgaria", 42.7f, 23.32f, 2.0f, CITY_DST_EU},
  {"St. Louis, USA", 38.63f, -90.2f, -6.0f, CITY_DST_US},
  {"St. Petersburg, Russia", 59.93f, 30.34f, 3.0f, CITY_DST_NONE},
  {"Stockholm, Sweden", 59.33f, 18.07f, 1.0f, CITY_DST_EU},
  {"Surabaya, Indonesia", -7.25f, 112.75f, 7.0f, CITY_DST_NONE},
  {"Sydney, Australia", -33.87f, 151.21f, 10.0f, CITY_DST_AU},
  {"Taipei, Taiwan", 25.03f, 121.57f, 8.0f, CITY_DST_NONE},
  {"Tallinn, Estonia", 59.44f, 24.75f, 2.0f, CITY_DST_EU},
  {"Tampa, USA", 27.95f, -82.46f, -5.0f, CITY_DST_US},
  {"Tashkent, Uzbekistan", 41.3f, 69.24f, 5.0f, CITY_DST_NONE},
  {"Tbilisi, Georgia", 41.72f, 44.79f, 4.0f, CITY_DST_NONE},
  {"Tehran, Iran", 35.69f, 51.39f, 3.5f, CITY_DST_NONE},
  {"Tel Aviv, Israel", 32.08f, 34.78f, 2.0f, CITY_DST_IL},
  {"Tianjin, China", 39.08f, 117.2f, 8.0f, CITY_DST_NONE},
  {"Tijuana, Mexico", 32.52f, -117.02f, -8.0f, CITY_DST_US},
  {"Tirana, Albania", 41.33f, 19.82f, 1.0f, CITY_DST_EU},
  {"Tokyo, Japan", 35.68f, 139.65f, 9.0f, CITY_DST_NONE},
  {"Toronto, Canada", 43.65f, -79.38f, -5.0f, CITY_DST_US},
  {"Tripoli, Libya", 32.89f, 13.19f, 2.0f, CITY_DST_NONE},
  {"Tunis, Tunisia", 36.81f, 10.18f, 1.0f, CITY_DST_NONE},
  {"Turin, Italy", 45.07f, 7.69f, 1.0f, CITY_DST_EU},
  {"Ulaanbaatar, Mongolia", 47.89f, 106.91f, 8.0f, CITY_DST_NONE},
  {"Valencia, Spain", 39.47f, -0.38f, 1.0f, CITY_DST_EU},
  {"Valletta, Malta", 35.9f, 14.51f, 1.0f, CITY_DST_EU},
  {"Vancouver, Canada", 49.28f, -123.12f, -8.0f, CITY_DST_US},
  {"Vienna, Austria", 48.21f, 16.37f, 1.0f, CITY_DST_EU},
  {"Vientiane, Laos", 17.97f, 102.6f, 7.0f, CITY_DST_NONE},
  {"Vilnius, Lithuania", 54.69f, 25.28f, 2.0f, CITY_DST_EU},
  {"Warsaw, Poland", 52.23f, 21.01f, 1.0f, CITY_DST_EU},
  {"Washington, USA", 38.91f, -77.04f, -5.0f, CITY_DST_US},
  {"Wellington, New Zealand", -41.29f, 174.78f, 12.0f, CITY_DST_NZ},
  {"Wuhan, China", 30.59f, 114.3f, 8.0f, CITY_DST_NONE},
  {"Xi'an, China", 34.34f, 108.94f, 8.0f, CITY_DST_NONE},
  {"Yangon, Myanmar", 16.87f, 96.2f, 6.5f, CITY_DST_NONE},
  {"Yerevan, Armenia", 40.18f, 44.51f, 4.0f, CITY_DST_NONE},
  {"Yokohama, Japan", 35.44f, 139.64f, 9.0f, CITY_DST_NONE},
  {"Zagreb, Croatia", 45.81f, 15.98f, 1.0f, CITY_DST_EU},
  {"Zurich, Switzerland", 47.38f, 8.54f, 1.0f, CITY_DST_EU},
};

