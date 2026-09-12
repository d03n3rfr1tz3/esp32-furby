// Local configuration template.
//
// Copy this file to `config.h` and fill it in. `config.h` is git-ignored and must never be
// committed — it is the only place credentials live (NFR-8).
//
//     cp firmware/config.example.h firmware/config.h
//
// Values here are placeholders, not defaults worth keeping.

#pragma once

// --- Identity ---------------------------------------------------------------------------

#define FURBY_HOSTNAME "furby"

// --- WiFi -------------------------------------------------------------------------------
// Two networks, tried in order, with automatic reconnection (F-01). Leave the secondary
// SSID empty to use only the primary.

#define FURBY_WIFI_SSID_PRIMARY "your-ssid"
#define FURBY_WIFI_PASS_PRIMARY "your-password"

#define FURBY_WIFI_SSID_SECONDARY ""
#define FURBY_WIFI_PASS_SECONDARY ""

// --- Over-the-air updates ---------------------------------------------------------------

#define FURBY_OTA_PASSWORD "change-me"

// --- Time -------------------------------------------------------------------------------
// POSIX timezone string; the default is Central European Time with EU daylight saving.

#define FURBY_NTP_SERVER "pool.ntp.org"
#define FURBY_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"

// Nothing beyond this point yet. Voice (ARCH-D2) gains its settings with F-42, and the
// control and event transport has none until ARCH-D4 is resolved. A setting is added here
// when the feature that reads it is built, not before.
