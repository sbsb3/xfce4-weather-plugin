/*  Copyright (c) 2003-2014 Xfce Development Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <string.h>
#include <math.h>
#include <time.h>

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "weather-parsers.h"
#include "weather-data.h"
#include "weather-icon.h"
#include "weather-debug.h"
#include "weather-ec.h"


/* Convert EC icon code (0-based integer) to symbol_id */
static gint
ec_icon_to_symbol_id(gint code)
{
    static const gint map[] = {
        /* 0  */ SYMBOL_SUN,
        /* 1  */ SYMBOL_LIGHTCLOUD,
        /* 2  */ SYMBOL_PARTLYCLOUD,
        /* 3  */ SYMBOL_PARTLYCLOUD,
        /* 4  */ SYMBOL_CLOUD,
        /* 5  */ SYMBOL_CLOUD,
        /* 6  */ SYMBOL_CLOUD,
        /* 7  */ SYMBOL_LIGHTRAINSUN,
        /* 8  */ SYMBOL_LIGHTRAINSUN,
        /* 9  */ SYMBOL_RAIN,
        /* 10 */ SYMBOL_LIGHTRAINTHUNDERSUN,
        /* 11 */ SYMBOL_RAINTHUNDER,
        /* 12 */ SYMBOL_SLEET,
        /* 13 */ SYMBOL_SNOWSUN,
        /* 14 */ SYMBOL_SLEETSUN,
        /* 15 */ SYMBOL_SNOWSUN,
        /* 16 */ SYMBOL_SNOW,
        /* 17 */ SYMBOL_LIGHTRAIN,
        /* 18 */ SYMBOL_LIGHTRAIN,
        /* 19 */ SYMBOL_RAIN,
        /* 20 */ SYMBOL_RAIN,
        /* 21 */ SYMBOL_SLEET,
        /* 22 */ SYMBOL_SLEET,
        /* 23 */ SYMBOL_SNOW,
        /* 24 */ SYMBOL_SNOW,
        /* 25 */ SYMBOL_SNOW,
        /* 26 */ SYMBOL_SNOW,
        /* 27 */ SYMBOL_SNOW,
        /* 28 */ SYMBOL_FOG,
        /* 29 */ SYMBOL_FOG,
        /* 30 */ SYMBOL_FOG,
        /* 31 */ SYMBOL_FOG,
        /* 32 */ SYMBOL_FOG,
        /* 33 */ SYMBOL_SUN,
        /* 34 */ SYMBOL_LIGHTCLOUD,
        /* 35 */ SYMBOL_PARTLYCLOUD,
        /* 36 */ SYMBOL_CLOUD,
        /* 37 */ SYMBOL_LIGHTRAINSUN,
        /* 38 */ SYMBOL_SNOWSUN,
        /* 39 */ SYMBOL_SLEETSUN,
        /* 40 */ SYMBOL_CLOUD,
        /* 41 */ SYMBOL_RAINTHUNDER,
        /* 42 */ SYMBOL_RAINTHUNDER,
        /* 43 */ SYMBOL_RAINTHUNDER,
        /* 44 */ SYMBOL_RAINTHUNDER,
        /* 45 */ SYMBOL_SLEET,
        /* 46 */ SYMBOL_SLEET,
        /* 47 */ SYMBOL_LIGHTRAIN,
        /* 48 */ SYMBOL_SLEET,
    };
    if (code < 0 || code >= (gint) G_N_ELEMENTS(map))
        return SYMBOL_NODATA;
    return map[code];
}


/* Wind speed in m/s to Beaufort scale */
static gint
mps_to_beaufort(gdouble mps)
{
    if (mps < 0.3)  return 0;
    if (mps < 1.6)  return 1;
    if (mps < 3.4)  return 2;
    if (mps < 5.5)  return 3;
    if (mps < 8.0)  return 4;
    if (mps < 10.8) return 5;
    if (mps < 13.9) return 6;
    if (mps < 17.2) return 7;
    if (mps < 20.8) return 8;
    if (mps < 24.5) return 9;
    if (mps < 28.5) return 10;
    if (mps < 32.7) return 11;
    return 12;
}


/* Parse EC timestamp "YYYYMMDDHHmmss" as UTC */
static time_t
ec_parse_timestamp(const gchar *ts)
{
    GDateTime *dt;
    time_t result = 0;
    int y, mo, d, h, mi, s;

    if (!ts || strlen(ts) < 14)
        return time(NULL);

    sscanf(ts, "%4d%2d%2d%2d%2d%2d", &y, &mo, &d, &h, &mi, &s);
    dt = g_date_time_new_utc(y, mo, d, h, mi, (gdouble) s);
    if (dt) {
        result = g_date_time_to_unix(dt);
        g_date_time_unref(dt);
    }
    return result > 0 ? result : time(NULL);
}


void
ec_station_free(ec_station *station)
{
    if (!station)
        return;
    g_free(station->province);
    g_free(station->station_id);
    g_free(station->name);
    g_slice_free(ec_station, station);
}


/*
 * Parse a single CSV line, handling quoted fields.
 * Returns a newly-allocated array of strings (NULL-terminated),
 * or NULL on failure. Caller frees with g_strfreev().
 */
static gchar **
ec_parse_csv_line(const gchar *line)
{
    GPtrArray *fields = g_ptr_array_new();
    const gchar *p = line;
    GString *field = g_string_new(NULL);
    gboolean in_quotes = FALSE;

    while (*p) {
        if (*p == '"') {
            in_quotes = !in_quotes;
        } else if (*p == ',' && !in_quotes) {
            g_ptr_array_add(fields, g_string_free(field, FALSE));
            field = g_string_new(NULL);
        } else {
            g_string_append_c(field, *p);
        }
        p++;
    }
    g_ptr_array_add(fields, g_string_free(field, FALSE));
    g_ptr_array_add(fields, NULL);

    return (gchar **) g_ptr_array_free(fields, FALSE);
}


/*
 * Haversine distance between two lat/lon points in km.
 */
static gdouble
haversine_km(gdouble lat1, gdouble lon1, gdouble lat2, gdouble lon2)
{
    const gdouble R = 6371.0;
    gdouble dlat = (lat2 - lat1) * G_PI / 180.0;
    gdouble dlon = (lon2 - lon1) * G_PI / 180.0;
    gdouble a;

    lat1 = lat1 * G_PI / 180.0;
    lat2 = lat2 * G_PI / 180.0;

    a = sin(dlat / 2) * sin(dlat / 2) +
        cos(lat1) * cos(lat2) * sin(dlon / 2) * sin(dlon / 2);
    return R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}


ec_station *
ec_find_nearest_from_csv(const gchar *csv_data, gsize len,
                          gdouble lat, gdouble lon)
{
    gchar **lines;
    guint i;
    ec_station *best = NULL;
    gdouble best_dist = G_MAXDOUBLE;

    if (!csv_data || len == 0)
        return NULL;

    lines = g_strsplit(csv_data, "\n", -1);
    if (!lines)
        return NULL;

    /* skip header line (index 0) */
    for (i = 1; lines[i] != NULL; i++) {
        gchar *line = g_strstrip(lines[i]);
        gchar **fields;
        gdouble slat, slon, dist;
        ec_station *st;

        if (!line || *line == '\0')
            continue;

        fields = ec_parse_csv_line(line);
        if (!fields)
            continue;

        /* fields: 0=code, 1=name, 2=province, 3=latitude, 4=longitude */
        if (!fields[0] || !fields[1] || !fields[2] || !fields[3] || !fields[4]) {
            g_strfreev(fields);
            continue;
        }

        /* skip HEF pseudo-province */
        if (strcmp(fields[2], "HEF") == 0) {
            g_strfreev(fields);
            continue;
        }

        slat = g_ascii_strtod(fields[3], NULL);
        slon = g_ascii_strtod(fields[4], NULL);

        /* Canadian longitudes are west, so make negative if positive */
        if (slon > 0.0)
            slon = -slon;

        dist = haversine_km(lat, lon, slat, slon);
        if (dist < best_dist) {
            best_dist = dist;
            if (best)
                ec_station_free(best);
            st = g_slice_new0(ec_station);
            st->station_id = g_strdup(fields[0]);
            st->name       = g_strdup(fields[1]);
            st->province   = g_strdup(fields[2]);
            st->latitude   = slat;
            st->longitude  = slon;
            best = st;
        }

        g_strfreev(fields);
    }

    g_strfreev(lines);
    return best;
}


gchar *
ec_find_xml_url_in_dirlist(const gchar *html, const gchar *dir_url,
                            const gchar *station_id)
{
    GRegex *regex;
    GMatchInfo *match_info;
    gchar *pattern;
    gchar *result = NULL;

    if (!html || !dir_url || !station_id)
        return NULL;

    pattern = g_strdup_printf(
        "href=\"([^\"]*MSC_CitypageWeather_%s_en\\.xml)\"",
        station_id);

    regex = g_regex_new(pattern, 0, 0, NULL);
    g_free(pattern);

    if (!regex)
        return NULL;

    if (g_regex_match(regex, html, 0, &match_info)) {
        gchar *filename = g_match_info_fetch(match_info, 1);
        if (filename) {
            /* filename may be relative or absolute */
            if (g_str_has_prefix(filename, "http")) {
                result = filename;
            } else {
                result = g_strconcat(dir_url, filename, NULL);
                g_free(filename);
            }
        }
    }

    g_match_info_free(match_info);
    g_regex_unref(regex);

    return result;
}


gboolean
ec_parse_weather(const gchar *data, gsize len, xml_weather *wd)
{
    xmlDoc *doc;
    xmlNode *root, *cur, *child;
    xmlChar *content;

    /* observation values */
    gdouble temp_c   = 0.0;
    gdouble dewpt_c  = 0.0;
    gdouble pressure_kpa = 0.0;
    gdouble humidity = 0.0;
    gdouble wind_kmh = 0.0;
    gdouble wind_bearing = 0.0;
    gchar  *wind_dir_name = NULL;
    gint    icon_code = -1;
    gchar  *condition_str = NULL;
    gchar  *timestamp_str = NULL;
    time_t  obs_time;

    gboolean got_temp     = FALSE;
    gboolean got_dewpt    = FALSE;
    gboolean got_pressure = FALSE;
    gboolean got_humidity = FALSE;
    gboolean got_wind     = FALSE;

    xml_time     *point1, *point2, *interval;
    xml_location *loc1,   *loc2,   *loci;
    gint          symbol_id;
    gdouble       wind_mps;
    gint          beaufort;

    if (!data || len == 0 || !wd)
        return FALSE;

    doc = xmlParseMemory(data, (int) len);
    if (!doc)
        return FALSE;

    root = xmlDocGetRootElement(doc);
    if (!root || xmlStrcmp(root->name, (const xmlChar *) "siteData") != 0) {
        xmlFreeDoc(doc);
        return FALSE;
    }

    /* find <currentConditions> */
    for (cur = root->children; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(cur->name, (const xmlChar *) "currentConditions") != 0)
            continue;

        for (child = cur->children; child; child = child->next) {
            if (child->type != XML_ELEMENT_NODE)
                continue;

            if (xmlStrcmp(child->name, (const xmlChar *) "dateTime") == 0) {
                /* EC XML has two dateTime[@name="observation"] elements:
                 * one with zone="UTC" and one with a local-time zone.
                 * We must use only the UTC one; parsing local time as
                 * UTC would produce an obs_time that is hours off. */
                xmlChar *nm   = xmlGetProp(child, (const xmlChar *) "name");
                xmlChar *zone = xmlGetProp(child, (const xmlChar *) "zone");
                if (nm && xmlStrcmp(nm, (const xmlChar *) "observation") == 0 &&
                    zone && xmlStrcmp(zone, (const xmlChar *) "UTC") == 0) {
                    xmlNode *ts_node;
                    for (ts_node = child->children; ts_node; ts_node = ts_node->next) {
                        if (ts_node->type == XML_ELEMENT_NODE &&
                            xmlStrcmp(ts_node->name, (const xmlChar *) "timeStamp") == 0) {
                            content = xmlNodeGetContent(ts_node);
                            if (content) {
                                g_free(timestamp_str);
                                timestamp_str = g_strdup((const gchar *) content);
                                xmlFree(content);
                            }
                            break;
                        }
                    }
                }
                xmlFree(nm);
                xmlFree(zone);
            } else if (xmlStrcmp(child->name, (const xmlChar *) "temperature") == 0) {
                content = xmlNodeGetContent(child);
                if (content) {
                    temp_c = g_ascii_strtod((const gchar *) content, NULL);
                    got_temp = TRUE;
                    xmlFree(content);
                }
            } else if (xmlStrcmp(child->name, (const xmlChar *) "dewpoint") == 0) {
                content = xmlNodeGetContent(child);
                if (content) {
                    dewpt_c = g_ascii_strtod((const gchar *) content, NULL);
                    got_dewpt = TRUE;
                    xmlFree(content);
                }
            } else if (xmlStrcmp(child->name, (const xmlChar *) "pressure") == 0) {
                content = xmlNodeGetContent(child);
                if (content) {
                    pressure_kpa = g_ascii_strtod((const gchar *) content, NULL);
                    got_pressure = TRUE;
                    xmlFree(content);
                }
            } else if (xmlStrcmp(child->name, (const xmlChar *) "relativeHumidity") == 0) {
                content = xmlNodeGetContent(child);
                if (content) {
                    humidity = g_ascii_strtod((const gchar *) content, NULL);
                    got_humidity = TRUE;
                    xmlFree(content);
                }
            } else if (xmlStrcmp(child->name, (const xmlChar *) "wind") == 0) {
                xmlNode *wn;
                for (wn = child->children; wn; wn = wn->next) {
                    if (wn->type != XML_ELEMENT_NODE)
                        continue;
                    if (xmlStrcmp(wn->name, (const xmlChar *) "speed") == 0) {
                        content = xmlNodeGetContent(wn);
                        if (content) {
                            wind_kmh = g_ascii_strtod((const gchar *) content, NULL);
                            got_wind = TRUE;
                            xmlFree(content);
                        }
                    } else if (xmlStrcmp(wn->name, (const xmlChar *) "direction") == 0) {
                        content = xmlNodeGetContent(wn);
                        if (content) {
                            g_free(wind_dir_name);
                            wind_dir_name = g_strdup((const gchar *) content);
                            xmlFree(content);
                        }
                    } else if (xmlStrcmp(wn->name, (const xmlChar *) "bearing") == 0) {
                        content = xmlNodeGetContent(wn);
                        if (content) {
                            wind_bearing = g_ascii_strtod((const gchar *) content, NULL);
                            xmlFree(content);
                        }
                    }
                }
            } else if (xmlStrcmp(child->name, (const xmlChar *) "iconCode") == 0) {
                content = xmlNodeGetContent(child);
                if (content) {
                    icon_code = atoi((const gchar *) content);
                    xmlFree(content);
                }
            } else if (xmlStrcmp(child->name, (const xmlChar *) "condition") == 0) {
                content = xmlNodeGetContent(child);
                if (content) {
                    g_free(condition_str);
                    condition_str = g_strdup((const gchar *) content);
                    xmlFree(content);
                }
            }
        }
        break; /* only process first currentConditions */
    }

    xmlFreeDoc(doc);

    if (!got_temp && !got_wind && !got_humidity && !got_pressure) {
        g_free(timestamp_str);
        g_free(wind_dir_name);
        g_free(condition_str);
        return FALSE;
    }

    obs_time = ec_parse_timestamp(timestamp_str);
    g_free(timestamp_str);

    wind_mps = wind_kmh / 3.6;
    beaufort = mps_to_beaufort(wind_mps);
    symbol_id = (icon_code >= 0) ? ec_icon_to_symbol_id(icon_code) : SYMBOL_NODATA;

    /* --- Point timeslice 1: obs_time --- */
    point1 = make_timeslice();
    if (!point1) {
        g_free(wind_dir_name);
        g_free(condition_str);
        return FALSE;
    }
    point1->start = obs_time;
    point1->end   = obs_time;
    point1->point = obs_time;
    loc1 = point1->location;

    if (got_temp)
        loc1->temperature_value = g_strdup_printf("%.1f", temp_c);
    loc1->temperature_unit = g_strdup("celsius");

    if (got_dewpt) {
        loc1->humidity_value = g_strdup_printf("%.0f", humidity);
        loc1->humidity_unit  = g_strdup("percent");
    }

    if (got_humidity) {
        /* overwrite with actual humidity value */
        g_free(loc1->humidity_value);
        loc1->humidity_value = g_strdup_printf("%.0f", humidity);
        g_free(loc1->humidity_unit);
        loc1->humidity_unit  = g_strdup("percent");
    }

    if (got_pressure) {
        /* kPa * 10 = hPa */
        loc1->pressure_value = g_strdup_printf("%.1f", pressure_kpa * 10.0);
        loc1->pressure_unit  = g_strdup("hPa");
    }

    if (got_wind) {
        loc1->wind_speed_mps      = g_strdup_printf("%.1f", wind_mps);
        loc1->wind_speed_beaufort = g_strdup_printf("%d", beaufort);
        if (wind_dir_name)
            loc1->wind_dir_name   = g_strdup(wind_dir_name);
        loc1->wind_dir_deg        = g_strdup_printf("%.1f", wind_bearing);
    }

    /* dewpoint */
    if (got_dewpt) {
        /* store dewpoint in temperature_value of a separate field is not
           directly available; the data model doesn't have a dedicated dewpt
           field in xml_location but get_data() reads it via DEWPOINT which
           uses temperature_value of a special timeslice... For now we just
           store it as wind data so callers can use DEWPOINT via the combined
           timeslice which picks it up from point data. Actually xml_location
           has no dewpoint_value field; DEWPOINT is computed from temp+humidity.
           So we only need temp and humidity, which we already set. */
        (void) dewpt_c; /* suppress unused warning */
    }

    merge_timeslice(wd, point1);
    xml_time_free(point1);

    /* --- Point timeslice 2: anchor for the interval end.
     * Placed 1 second before the next full hour (obs_time+3599) so it
     * cannot be overwritten by an hourly forecast entry that always
     * lands on an exact hour boundary.  Keeping the anchor's temperature
     * equal to the observation prevents make_combined_timeslice from
     * interpolating toward a future forecast value while the observed
     * conditions are still current. --- */
    point2 = make_timeslice();
    if (!point2) {
        g_free(wind_dir_name);
        g_free(condition_str);
        return TRUE; /* partial success */
    }
    point2->start = obs_time + 3599;
    point2->end   = obs_time + 3599;
    point2->point = obs_time + 3599;
    loc2 = point2->location;

    if (got_temp)
        loc2->temperature_value = g_strdup_printf("%.1f", temp_c);
    loc2->temperature_unit = g_strdup("celsius");

    if (got_humidity) {
        loc2->humidity_value = g_strdup_printf("%.0f", humidity);
        loc2->humidity_unit  = g_strdup("percent");
    }

    if (got_pressure) {
        loc2->pressure_value = g_strdup_printf("%.1f", pressure_kpa * 10.0);
        loc2->pressure_unit  = g_strdup("hPa");
    }

    if (got_wind) {
        loc2->wind_speed_mps      = g_strdup_printf("%.1f", wind_mps);
        loc2->wind_speed_beaufort = g_strdup_printf("%d", beaufort);
        if (wind_dir_name)
            loc2->wind_dir_name   = g_strdup(wind_dir_name);
        loc2->wind_dir_deg        = g_strdup_printf("%.1f", wind_bearing);
    }

    merge_timeslice(wd, point2);
    xml_time_free(point2);

    /* --- Interval timeslice: obs_time to obs_time+3600 --- */
    interval = make_timeslice();
    if (!interval) {
        g_free(wind_dir_name);
        g_free(condition_str);
        return TRUE; /* partial success */
    }
    interval->start = obs_time;
    interval->end   = obs_time + 3599;
    interval->point = obs_time;
    loci = interval->location;

    loci->symbol_id = symbol_id;
    /* Encode EC icon code in symbol string so callers can load EC PNG directly */
    loci->symbol    = (icon_code >= 0)
        ? g_strdup_printf("EC:%02d", icon_code)
        : g_strdup(get_symbol_name(symbol_id));
    loci->precipitation_value = g_strdup("0.0");
    loci->precipitation_unit  = g_strdup("mm");

    merge_timeslice(wd, interval);
    xml_time_free(interval);

    g_free(wind_dir_name);
    g_free(condition_str);

    weather_debug("EC weather parsed: temp=%.1f hPa=%.1f wind_mps=%.1f symbol=%d",
                  temp_c, pressure_kpa * 10.0, wind_mps, symbol_id);

    return TRUE;
}


/* Parse hourly forecast timestamp "YYYYMMDDHHMM" (12-char UTC) → time_t */
static time_t
ec_parse_timestamp_hourly(const gchar *ts)
{
    GDateTime *dt;
    time_t result = 0;
    int y, mo, d, h, mi;

    if (!ts || strlen(ts) < 10)
        return 0;

    /* format is YYYYMMDDHHMM (12 chars) or YYYYMMDDHH (10 chars) */
    if (strlen(ts) >= 12)
        sscanf(ts, "%4d%2d%2d%2d%2d", &y, &mo, &d, &h, &mi);
    else
        sscanf(ts, "%4d%2d%2d%2d", &y, &mo, &d, &h), mi = 0;

    dt = g_date_time_new_utc(y, mo, d, h, mi, 0.0);
    if (dt) {
        result = g_date_time_to_unix(dt);
        g_date_time_unref(dt);
    }
    return result;
}


/*
 * Parse EC hourly and daily forecasts into xml_weather timeslices.
 * obs_time: UTC time of observation (from currentConditions).
 * Returns TRUE if any forecasts were added.
 */
gboolean
ec_parse_forecasts(const gchar *data, gsize len, xml_weather *wd,
                   time_t obs_time)
{
    xmlDoc   *doc;
    xmlNode  *root, *cur, *forecast_node, *child, *wn;
    xmlChar  *content;
    gboolean  added = FALSE;

    if (!data || len == 0 || !wd)
        return FALSE;

    doc = xmlParseMemory(data, (int) len);
    if (!doc)
        return FALSE;

    root = xmlDocGetRootElement(doc);
    if (!root || xmlStrcmp(root->name, (const xmlChar *) "siteData") != 0) {
        xmlFreeDoc(doc);
        return FALSE;
    }

    /* ── Hourly forecasts ── */
    for (cur = root->children; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(cur->name, (const xmlChar *) "hourlyForecastGroup") != 0)
            continue;

        for (forecast_node = cur->children;
             forecast_node; forecast_node = forecast_node->next) {
            if (forecast_node->type != XML_ELEMENT_NODE)
                continue;
            if (xmlStrcmp(forecast_node->name,
                          (const xmlChar *) "hourlyForecast") != 0)
                continue;

            xmlChar *ts_attr = xmlGetProp(forecast_node,
                                          (const xmlChar *) "dateTimeUTC");
            if (!ts_attr)
                continue;

            time_t fc_time = ec_parse_timestamp_hourly((const gchar *) ts_attr);
            xmlFree(ts_attr);

            if (fc_time == 0)
                continue;

            /* Observed conditions at obs_time take priority over forecast
             * data for the same hour; skip any hourly entry that doesn't
             * strictly follow the observation. */
            if (difftime(fc_time, obs_time) <= 0)
                continue;

            gdouble h_temp    = 0.0;
            gdouble h_wind_kmh = 0.0;
            gchar  *h_wind_dir = NULL;
            gint    h_icon     = -1;
            gint    h_pop      = 0;
            gboolean h_got_temp = FALSE, h_got_wind = FALSE;

            for (child = forecast_node->children; child; child = child->next) {
                if (child->type != XML_ELEMENT_NODE)
                    continue;

                if (xmlStrcmp(child->name, (const xmlChar *) "temperature") == 0) {
                    content = xmlNodeGetContent(child);
                    if (content) {
                        h_temp = g_ascii_strtod((const gchar *) content, NULL);
                        h_got_temp = TRUE;
                        xmlFree(content);
                    }
                } else if (xmlStrcmp(child->name, (const xmlChar *) "iconCode") == 0) {
                    content = xmlNodeGetContent(child);
                    if (content) {
                        h_icon = atoi((const gchar *) content);
                        xmlFree(content);
                    }
                } else if (xmlStrcmp(child->name, (const xmlChar *) "lop") == 0) {
                    content = xmlNodeGetContent(child);
                    if (content) {
                        h_pop = atoi((const gchar *) content);
                        xmlFree(content);
                    }
                } else if (xmlStrcmp(child->name, (const xmlChar *) "wind") == 0) {
                    for (wn = child->children; wn; wn = wn->next) {
                        if (wn->type != XML_ELEMENT_NODE)
                            continue;
                        if (xmlStrcmp(wn->name, (const xmlChar *) "speed") == 0) {
                            content = xmlNodeGetContent(wn);
                            if (content) {
                                h_wind_kmh = g_ascii_strtod(
                                    (const gchar *) content, NULL);
                                h_got_wind = TRUE;
                                xmlFree(content);
                            }
                        } else if (xmlStrcmp(wn->name,
                                             (const xmlChar *) "direction") == 0) {
                            /* prefer windDirFull attribute, fall back to text */
                            xmlChar *full = xmlGetProp(
                                wn, (const xmlChar *) "windDirFull");
                            if (full) {
                                g_free(h_wind_dir);
                                h_wind_dir = g_strdup((const gchar *) full);
                                xmlFree(full);
                            } else {
                                content = xmlNodeGetContent(wn);
                                if (content) {
                                    g_free(h_wind_dir);
                                    h_wind_dir = g_strdup(
                                        (const gchar *) content);
                                    xmlFree(content);
                                }
                            }
                        }
                    }
                }
            }

            /* Build point timeslice */
            xml_time     *pt  = make_timeslice();
            xml_location *loc;
            if (!pt) {
                g_free(h_wind_dir);
                continue;
            }
            pt->start = fc_time;
            pt->end   = fc_time;
            pt->point = fc_time;
            loc = pt->location;

            if (h_got_temp) {
                loc->temperature_value = g_strdup_printf("%.1f", h_temp);
                loc->temperature_unit  = g_strdup("celsius");
            }
            if (h_got_wind) {
                gdouble mps = h_wind_kmh / 3.6;
                loc->wind_speed_mps      = g_strdup_printf("%.1f", mps);
                loc->wind_speed_beaufort = g_strdup_printf(
                    "%d", mps_to_beaufort(mps));
                if (h_wind_dir)
                    loc->wind_dir_name = g_strdup(h_wind_dir);
            }

            merge_timeslice(wd, pt);
            xml_time_free(pt);
            added = TRUE;

            /* Build interval timeslice [fc_time, fc_time+3600] with symbol */
            gint sym_id = (h_icon >= 0) ?
                ec_icon_to_symbol_id(h_icon) : SYMBOL_NODATA;

            xml_time     *iv   = make_timeslice();
            xml_location *loci;
            if (iv) {
                iv->start = fc_time;
                iv->end   = fc_time + 3600;
                iv->point = fc_time;
                loci = iv->location;

                loci->symbol_id = sym_id;
                loci->symbol    = g_strdup_printf("EC:%02d", h_icon);
                if (h_pop > 0)
                    loci->precipitation_value = g_strdup_printf("%.1f",
                                                                 h_pop * 0.1);
                else
                    loci->precipitation_value = g_strdup("0.0");
                loci->precipitation_unit = g_strdup("mm");

                merge_timeslice(wd, iv);
                xml_time_free(iv);
            }

            g_free(h_wind_dir);
        }
        break; /* only one hourlyForecastGroup */
    }

    /* ── Daily forecasts ── */
    for (cur = root->children; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(cur->name, (const xmlChar *) "forecastGroup") != 0)
            continue;

        /* Use the LOCAL calendar date of the observation as the base.
         * We advance cur_day whenever a daytime period follows a night
         * period, so the sequence Tonight/Tuesday/Tuesday night/Wednesday…
         * or Today/Tonight/Tuesday… both land on the right calendar days.
         * Hours are set in LOCAL time (14:00 = mid-afternoon, 23:00 = night)
         * so the summary-window columns (Morning/Afternoon/Evening/Night)
         * are populated correctly regardless of the observer's UTC offset. */
        struct tm obs_local;
        localtime_r(&obs_time, &obs_local);
        gint     cur_day       = 0;
        gboolean prev_was_night = FALSE;

        for (forecast_node = cur->children;
             forecast_node; forecast_node = forecast_node->next) {
            if (forecast_node->type != XML_ELEMENT_NODE)
                continue;
            if (xmlStrcmp(forecast_node->name,
                          (const xmlChar *) "forecast") != 0)
                continue;

            gchar   *period_name = NULL;
            gdouble  fc_temp     = 0.0;
            gboolean fc_got_temp = FALSE;
            gint     fc_icon     = -1;

            for (child = forecast_node->children; child; child = child->next) {
                if (child->type != XML_ELEMENT_NODE)
                    continue;

                if (xmlStrcmp(child->name, (const xmlChar *) "period") == 0) {
                    xmlChar *attr = xmlGetProp(
                        child, (const xmlChar *) "textForecastName");
                    if (attr) {
                        g_free(period_name);
                        period_name = g_strdup((const gchar *) attr);
                        xmlFree(attr);
                    }
                } else if (xmlStrcmp(child->name,
                                     (const xmlChar *) "temperatures") == 0) {
                    xmlNode *tn;
                    for (tn = child->children; tn; tn = tn->next) {
                        if (tn->type != XML_ELEMENT_NODE)
                            continue;
                        if (xmlStrcmp(tn->name,
                                      (const xmlChar *) "temperature") == 0) {
                            content = xmlNodeGetContent(tn);
                            if (content) {
                                fc_temp = g_ascii_strtod(
                                    (const gchar *) content, NULL);
                                fc_got_temp = TRUE;
                                xmlFree(content);
                                break; /* take first temperature */
                            }
                        }
                    }
                } else if (xmlStrcmp(child->name,
                                     (const xmlChar *) "abbreviatedForecast") == 0) {
                    xmlNode *an;
                    for (an = child->children; an; an = an->next) {
                        if (an->type != XML_ELEMENT_NODE)
                            continue;
                        if (xmlStrcmp(an->name,
                                      (const xmlChar *) "iconCode") == 0) {
                            content = xmlNodeGetContent(an);
                            if (content) {
                                fc_icon = atoi((const gchar *) content);
                                xmlFree(content);
                            }
                        }
                    }
                }
            }

            if (!fc_got_temp && fc_icon < 0) {
                g_free(period_name);
                continue;
            }

            gboolean is_night = period_name &&
                (g_ascii_strncasecmp(period_name, "Tonight", 7) == 0 ||
                 strstr(period_name, "night") != NULL ||
                 strstr(period_name, "Night") != NULL);

            /* Advance the calendar day when a daytime period follows a
             * night period (night→day boundary = new calendar day). */
            if (!is_night && prev_was_night)
                cur_day++;

            /* Build the forecast time in LOCAL time so it lands in the
             * correct Morning/Afternoon/Evening/Night column. */
            struct tm fc_local = obs_local;
            fc_local.tm_mday += cur_day;
            fc_local.tm_hour  = is_night ? 23 : 14;
            fc_local.tm_min   = 0;
            fc_local.tm_sec   = 0;
            fc_local.tm_isdst = -1;
            time_t fc_point   = mktime(&fc_local);

            /* Create point timeslice for this forecast period */
            xml_time *pt = make_timeslice();
            if (pt) {
                pt->start = fc_point;
                pt->end   = fc_point;
                pt->point = fc_point;
                xml_location *loc = pt->location;
                if (fc_got_temp) {
                    loc->temperature_value = g_strdup_printf("%.1f", fc_temp);
                    loc->temperature_unit  = g_strdup("celsius");
                }
                merge_timeslice(wd, pt);
                xml_time_free(pt);
                added = TRUE;
            }

            /* Create interval timeslice with symbol */
            if (fc_icon >= 0) {
                gint sym_id = ec_icon_to_symbol_id(fc_icon);
                xml_time *iv = make_timeslice();
                if (iv) {
                    iv->start = fc_point;
                    iv->end   = fc_point + 12 * 3600;
                    iv->point = fc_point;
                    xml_location *loci = iv->location;
                    loci->symbol_id = sym_id;
                    loci->symbol    = g_strdup_printf("EC:%02d", fc_icon);
                    loci->precipitation_value = g_strdup("0.0");
                    loci->precipitation_unit  = g_strdup("mm");
                    merge_timeslice(wd, iv);
                    xml_time_free(iv);
                }
            }

            prev_was_night = is_night;
            g_free(period_name);
        }
        break; /* only one forecastGroup */
    }

    xmlFreeDoc(doc);
    weather_debug("EC: ec_parse_forecasts() added=%d", (int) added);
    return added;
}


/*
 * Parse EC weather warnings/alerts from the XML.
 * Returns a newly-allocated GPtrArray of ec_alert*, or NULL.
 * The caller must free with g_ptr_array_unref() (alerts freed via notify func).
 */
GPtrArray *
ec_parse_alerts(const gchar *data, gsize len)
{
    xmlDoc  *doc;
    xmlNode *root, *cur, *event_node;
    GPtrArray *alerts;

    if (!data || len == 0)
        return NULL;

    doc = xmlParseMemory(data, (int) len);
    if (!doc)
        return NULL;

    root = xmlDocGetRootElement(doc);
    if (!root || xmlStrcmp(root->name, (const xmlChar *) "siteData") != 0) {
        xmlFreeDoc(doc);
        return NULL;
    }

    alerts = g_ptr_array_new_with_free_func((GDestroyNotify) ec_alert_free);

    for (cur = root->children; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(cur->name, (const xmlChar *) "warnings") != 0)
            continue;

        for (event_node = cur->children;
             event_node; event_node = event_node->next) {
            if (event_node->type != XML_ELEMENT_NODE)
                continue;
            if (xmlStrcmp(event_node->name, (const xmlChar *) "event") != 0)
                continue;

            ec_alert *alert = g_slice_new0(ec_alert);

            xmlChar *type  = xmlGetProp(event_node, (const xmlChar *) "type");
            xmlChar *descr = xmlGetProp(event_node,
                                        (const xmlChar *) "description");
            xmlChar *expir = xmlGetProp(event_node,
                                        (const xmlChar *) "expiryTime");
            xmlChar *url   = xmlGetProp(event_node, (const xmlChar *) "url");

            alert->type        = type  ? g_strdup((const gchar *) type)  : NULL;
            alert->description = descr ? g_strdup((const gchar *) descr) : NULL;
            alert->expiry      = expir ? g_strdup((const gchar *) expir) : NULL;
            alert->url         = url   ? g_strdup((const gchar *) url)   : NULL;

            xmlFree(type);
            xmlFree(descr);
            xmlFree(expir);
            xmlFree(url);

            g_ptr_array_add(alerts, alert);
        }
        break; /* only one <warnings> */
    }

    xmlFreeDoc(doc);
    weather_debug("EC: ec_parse_alerts() found %u alerts", alerts->len);
    return alerts;
}


void
ec_alert_free(ec_alert *alert)
{
    if (!alert)
        return;
    g_free(alert->type);
    g_free(alert->description);
    g_free(alert->expiry);
    g_free(alert->url);
    g_slice_free(ec_alert, alert);
}


/*
 * Parse the AQHI site list XML and return the region nearest to (lat, lon).
 * Returns a newly-allocated ec_aqhi_region, or NULL. Caller frees.
 */
ec_aqhi_region *
ec_find_nearest_aqhi_region(const gchar *xml_data, gsize len,
                             gdouble lat, gdouble lon)
{
    xmlDoc  *doc;
    xmlNode *root, *zone_node, *region_list, *region_node;
    ec_aqhi_region *best   = NULL;
    gdouble         best_d = G_MAXDOUBLE;

    if (!xml_data || len == 0)
        return NULL;

    doc = xmlParseMemory(xml_data, (int) len);
    if (!doc)
        return NULL;

    root = xmlDocGetRootElement(doc);
    if (!root) {
        xmlFreeDoc(doc);
        return NULL;
    }

    for (zone_node = root->children; zone_node; zone_node = zone_node->next) {
        if (zone_node->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(zone_node->name,
                      (const xmlChar *) "EC_administrativeZone") != 0)
            continue;

        xmlChar *zone_id = xmlGetProp(zone_node,
                                      (const xmlChar *) "abreviation");
        if (!zone_id)
            continue;

        for (region_list = zone_node->children;
             region_list; region_list = region_list->next) {
            if (region_list->type != XML_ELEMENT_NODE)
                continue;
            if (xmlStrcmp(region_list->name,
                          (const xmlChar *) "regionList") != 0)
                continue;

            for (region_node = region_list->children;
                 region_node; region_node = region_node->next) {
                if (region_node->type != XML_ELEMENT_NODE)
                    continue;
                if (xmlStrcmp(region_node->name,
                              (const xmlChar *) "region") != 0)
                    continue;

                xmlChar *cgndb = xmlGetProp(region_node,
                                            (const xmlChar *) "cgndb");
                xmlChar *rlat  = xmlGetProp(region_node,
                                            (const xmlChar *) "latitude");
                xmlChar *rlon  = xmlGetProp(region_node,
                                            (const xmlChar *) "longitude");
                xmlChar *rname = xmlGetProp(region_node,
                                            (const xmlChar *) "nameEn");

                if (cgndb && rlat && rlon) {
                    gdouble slat = g_ascii_strtod((const gchar *) rlat, NULL);
                    gdouble slon = g_ascii_strtod((const gchar *) rlon, NULL);
                    gdouble d    = haversine_km(lat, lon, slat, slon);
                    if (d < best_d) {
                        best_d = d;
                        ec_aqhi_region_free(best);
                        best = g_slice_new0(ec_aqhi_region);
                        best->zone_id     = g_strdup((const gchar *) zone_id);
                        best->region_id   = g_strdup((const gchar *) cgndb);
                        best->region_name = rname ?
                            g_strdup((const gchar *) rname) : NULL;
                        best->latitude    = slat;
                        best->longitude   = slon;
                    }
                }

                xmlFree(cgndb);
                xmlFree(rlat);
                xmlFree(rlon);
                xmlFree(rname);
            }
        }

        xmlFree(zone_id);
    }

    xmlFreeDoc(doc);
    return best;
}


void
ec_aqhi_region_free(ec_aqhi_region *region)
{
    if (!region)
        return;
    g_free(region->zone_id);
    g_free(region->region_id);
    g_free(region->region_name);
    g_slice_free(ec_aqhi_region, region);
}


/*
 * Parse the AQHI observation XML and return the AQHI value,
 * or -1.0 on failure.
 */
gdouble
ec_parse_aqhi_observation(const gchar *xml_data, gsize len)
{
    xmlDoc  *doc;
    xmlNode *root, *cur;
    gdouble  result = -1.0;

    if (!xml_data || len == 0)
        return -1.0;

    doc = xmlParseMemory(xml_data, (int) len);
    if (!doc)
        return -1.0;

    root = xmlDocGetRootElement(doc);
    if (!root) {
        xmlFreeDoc(doc);
        return -1.0;
    }

    for (cur = root->children; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(cur->name,
                      (const xmlChar *) "airQualityHealthIndex") == 0) {
            xmlChar *content = xmlNodeGetContent(cur);
            if (content) {
                result = g_ascii_strtod((const gchar *) content, NULL);
                xmlFree(content);
            }
            break;
        }
    }

    xmlFreeDoc(doc);
    return result;
}


/*
 * Load an EC icon PNG by icon code from the installed ec-icons directory.
 * Returns a cairo_surface_t scaled to (size * scale) px, or NULL on failure.
 */
cairo_surface_t *
ec_get_icon(gint icon_code, gint size, gint scale)
{
    gchar           *path;
    GdkPixbuf       *pb;
    cairo_surface_t *surface;
    gint             px;

    if (icon_code < 0)
        return NULL;

    px   = size * scale;
    path = g_strdup_printf(PACKAGE_DATADIR "/ec-icons/%02d.png", icon_code);
    pb   = gdk_pixbuf_new_from_file_at_scale(path, px, px, TRUE, NULL);
    g_free(path);

    if (!pb)
        return NULL;

    surface = gdk_cairo_surface_create_from_pixbuf(pb, scale, NULL);
    g_object_unref(pb);
    return surface;
}
