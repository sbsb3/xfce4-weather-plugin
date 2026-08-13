# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure build
meson setup build

# Compile
meson compile -C build

# Install
meson install -C build

# Uninstall
ninja uninstall -C build
```

Build options (pass to `meson setup`):
- `-Dupower-glib=enabled|disabled|auto` — power-saving support via upower
- `-Dgeonames-username=<name>` — GeoNames API username (default: `xfce4weatherplugin`)

Dependencies: glib >= 2.64, gtk >= 3.22, libxfce4 >= 4.16, libsoup >= 3.0 (or 2.4), libxml >= 2.4, json-c >= 0.13.1.

## Testing & Debugging

There is no automated test suite. Manual testing:
```bash
# Run panel with weather plugin debug output
PANEL_DEBUG=weather xfce4-panel
```

## Architecture

All plugin source is in `panel-plugin/`. The plugin fetches weather from **met.no** APIs, parses and caches the data, and displays it in the XFCE4 panel.

**Data Flow:**
1. `weather.c` — Main entry point, HTTP download queue, cache management, update timers, upower power-saving integration
2. `weather-parsers.c` — XML (libxml2) for met.no locationforecast API; JSON (json-c) for sunrise/sunset, geolocation, GeoNames
3. `weather-data.c` — Unit conversions (temperature, pressure, wind, precipitation, altitude), time/astronomical calculations, data merging/interpolation
4. Display layer:
   - Panel icon (inline in `weather.c`)
   - `weather-scrollbox.c` — Custom `GtkDrawingArea` widget with animated scrolling labels
   - `weather-summary.c` — Forecast popup window (calendar/list layout)
5. `weather-config.c` — Settings dialog (defined in `weather-config.ui` GResource), persisted via xfconf
6. `weather-search.c` — Location search dialog via GeoNames API
7. `weather-icon.c` — Icon theme loading/caching with night variants; 3 bundled themes (Liquid, Liquid-Dark, Simplistic)

**Key structs:**
- `plugin_data` (weather.h) — Central plugin state: settings, cached data, timers, UI references
- `xml_weather` / `xml_time` / `xml_astro` (weather-parsers.h) — Parsed API response data
- Unit enums and conversion functions live in `weather-data.h`

**External APIs:**
- `met.no locationforecast/2.0` — Forecasts (XML)
- `met.no sunrise/2.0` — Sunrise/sunset (JSON)
- `GeoNames` — Location search, altitude, timezone

**Translations:** 56 languages in `po/`. Use `msgfmt` to validate `.po` files. Translation support is integrated into the Meson build.

**Configuration** is stored via xfconf (Xfce config daemon), not flat files.
