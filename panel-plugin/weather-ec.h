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

#ifndef __WEATHER_EC_H__
#define __WEATHER_EC_H__

#include <glib.h>
#include "weather-parsers.h"

G_BEGIN_DECLS

#define EC_SITE_LIST_URL  "https://dd.weather.gc.ca/today/citypage_weather/docs/site_list_towns_en.csv"
#define EC_WEATHER_BASE   "https://dd.weather.gc.ca/today/citypage_weather/"
#define EC_MAX_HOURS_BACK 3

#define EC_AQHI_SITE_LIST_URL "https://dd.weather.gc.ca/today/air_quality/doc/AQHI_XML_File_List.xml"
#define EC_AQHI_OBS_URL       "https://dd.weather.gc.ca/today/air_quality/aqhi/%s/observation/realtime/xml/AQ_OBS_%s_CURRENT.xml"

typedef struct {
    gchar    *province;
    gchar    *station_id;
    gchar    *name;
    gdouble   latitude;
    gdouble   longitude;
} ec_station;

typedef struct {
    gchar    *type;        /* "advisory", "watch", "warning", "statement", "ended" */
    gchar    *description; /* e.g. "WEATHER ADVISORY" */
    gchar    *expiry;      /* expiry timestamp string (may be NULL) */
    gchar    *url;         /* URL for more info (may be NULL) */
} ec_alert;

typedef struct {
    gchar    *zone_id;     /* e.g. "ont" */
    gchar    *region_id;   /* 5-char cgndb code e.g. "CAXYZ" */
    gchar    *region_name;
    gdouble   latitude;
    gdouble   longitude;
} ec_aqhi_region;

void       ec_station_free              (ec_station *station);
ec_station *ec_find_nearest_from_csv   (const gchar *csv_data, gsize len,
                                         gdouble lat, gdouble lon);
gchar     *ec_find_xml_url_in_dirlist  (const gchar *html, const gchar *dir_url,
                                         const gchar *station_id);
gboolean   ec_parse_weather            (const gchar *data, gsize len,
                                         xml_weather *wd);
gboolean   ec_parse_forecasts          (const gchar *data, gsize len,
                                         xml_weather *wd, time_t obs_time);
GPtrArray *ec_parse_alerts             (const gchar *data, gsize len);
void       ec_alert_free               (ec_alert *alert);

ec_aqhi_region *ec_find_nearest_aqhi_region (const gchar *xml_data, gsize len,
                                              gdouble lat, gdouble lon);
void       ec_aqhi_region_free         (ec_aqhi_region *region);
gdouble    ec_parse_aqhi_observation   (const gchar *xml_data, gsize len);

G_END_DECLS

#endif /* __WEATHER_EC_H__ */
