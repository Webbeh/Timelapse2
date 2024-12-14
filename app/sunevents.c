#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <math.h>
#include <glib.h>
#include "ACAP.h"
#include "cJSON.h"
#include "sunevents.h"

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_WARN(fmt, args...)    { syslog(LOG_WARNING, fmt, ## args); printf(fmt, ## args); }
//#define LOG_TRACE(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_TRACE(fmt, args...)    {}

static void Calculate_Sun_Events(double lat, double lon);

static cJSON* SunEventsSettings = NULL;
static GSource* timer_source = NULL;
static GSource* sunnoon_timer = NULL;

static gboolean SunNoon_Timer_Callback(gpointer user_data) {
    LOG_TRACE("%s: Sun noon event triggered\n", __func__);
    ACAP_EVENTS_Fire("sunnoon");
    return G_SOURCE_CONTINUE;
}

static void Setup_SunNoon_Timer(time_t noon) {
    time_t now;
    time(&now);
    
    // Calculate seconds until next sun noon
    int seconds_to_noon = noon - now;
    if (seconds_to_noon < 0) {
        seconds_to_noon += 24 * 3600; // Add 24 hours if noon already passed
    }
    
    LOG_TRACE("%s: Setting up sun noon timer for %d seconds\n", __func__, seconds_to_noon);
    
    if (sunnoon_timer) {
        g_source_destroy(sunnoon_timer);
        g_source_unref(sunnoon_timer);
    }
    
    sunnoon_timer = g_timeout_source_new_seconds(seconds_to_noon);
    g_source_set_callback(sunnoon_timer, SunNoon_Timer_Callback, NULL, NULL);
    g_source_attach(sunnoon_timer, NULL);
}



// Timer callback for midnight recalculation
static gboolean Timer_Callback(gpointer user_data) {
    LOG_TRACE("%s: Midnight timer triggered\n", __func__);
    
    double lat = cJSON_GetObjectItem(SunEventsSettings, "lat")->valuedouble;
    double lon = cJSON_GetObjectItem(SunEventsSettings, "lon")->valuedouble;
    Calculate_Sun_Events(lat, lon);
    
    return G_SOURCE_CONTINUE;
}

// Setup timer for next midnight
static void Setup_Midnight_Timer() {
    if (!SunEventsSettings) return;
    
    time_t now;
    time(&now);
    struct tm* local = localtime(&now);
    
    int seconds_to_midnight = ((23 - local->tm_hour) * 3600) + 
                            ((59 - local->tm_min) * 60) + 
                            (60 - local->tm_sec);
    
    LOG_TRACE("%s: Setting up timer for %d seconds\n", __func__, seconds_to_midnight);
    
    if (timer_source) {
        g_source_destroy(timer_source);
        g_source_unref(timer_source);
    }
    
    timer_source = g_timeout_source_new_seconds(seconds_to_midnight);
    if (!timer_source) {
        LOG_WARN("%s: Failed to create timer source\n", __func__);
        return;
    }
    
    g_source_set_callback(timer_source, Timer_Callback, NULL, NULL);
    g_source_attach(timer_source, NULL);
}

int SunEvents_Set(cJSON* location) {
    if (!location || !SunEventsSettings) return -1;
	
    // Debug print current settings
    char *debug_str = cJSON_PrintUnformatted(location);
    if (debug_str) {
        LOG_TRACE("%s: Updatating location: %s\n", __func__, debug_str);
        free(debug_str);
    }	
    
    cJSON* lon_obj = cJSON_GetObjectItem(location, "lon");
    cJSON* lat_obj = cJSON_GetObjectItem(location, "lat");
    if (!lon_obj || !lat_obj) return -1;
    
    double lon = lon_obj->valuedouble;
    double lat = lat_obj->valuedouble;
    
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) {
        LOG_WARN("%s: Invalid coordinates lat=%f, lon=%f\n", __func__, lat, lon);
        return -1;
    }
    
    if (!SunEventsSettings) {
		SunEventsSettings = cJSON_CreateObject();
		cJSON_AddNumberToObject(SunEventsSettings, "lat", lat);
		cJSON_AddNumberToObject(SunEventsSettings, "lon", lon);
		cJSON_AddNumberToObject(SunEventsSettings, "dawn", 0);
		cJSON_AddNumberToObject(SunEventsSettings, "sunrise", 0);
		cJSON_AddNumberToObject(SunEventsSettings, "sunnoon", 0);
		cJSON_AddNumberToObject(SunEventsSettings, "sunset", 0);
		cJSON_AddNumberToObject(SunEventsSettings, "dusk", 0);
	}
	
    LOG_TRACE("%s: Setting location to lat=%f, lon=%f\n", __func__, lat, lon);
    Calculate_Sun_Events(lat, lon);
    return 0;
}

int SunEvents_Between_Dawn_Dusk() {
    if (!SunEventsSettings) return 0;
    
    cJSON* dawn_obj = cJSON_GetObjectItem(SunEventsSettings, "dawn");
    cJSON* dusk_obj = cJSON_GetObjectItem(SunEventsSettings, "dusk");
    if (!dawn_obj || !dusk_obj) return 0;
    
    time_t now;
    time(&now);
    return (now >= dawn_obj->valuedouble && now <= dusk_obj->valuedouble) ? 1 : 0;
}

int SunEvents_Between_Sunrise_Sunset() {
    if (!SunEventsSettings) return 0;
    
    cJSON* sunrise_obj = cJSON_GetObjectItem(SunEventsSettings, "sunrise");
    cJSON* sunset_obj = cJSON_GetObjectItem(SunEventsSettings, "sunset");
    if (!sunrise_obj || !sunset_obj) return 0;
    
    time_t now;
    time(&now);
    return (now >= sunrise_obj->valuedouble && now <= sunset_obj->valuedouble) ? 1 : 0;
}

static void HTTP_Endpoint_Sunevents(const ACAP_HTTP_Response response, const ACAP_HTTP_Request request) {
    const char* method = ACAP_HTTP_Get_Method(request);
    if (!method) {
        LOG_WARN("%s: Invalid Request Method\n", __func__);
        ACAP_HTTP_Respond_Error(response, 400, "Invalid Request Method");
        return;
    }
    
    LOG_TRACE("%s: Method=%s\n", __func__, method);
    
    if (strcmp(method, "POST") == 0) {
        if (!request->postData) {
            ACAP_HTTP_Respond_Error(response, 400, "Missing POST data");
            return;
        }
        
        cJSON* location = cJSON_Parse(request->postData);
        if (!location) {
            ACAP_HTTP_Respond_Error(response, 400, "Invalid JSON data");
            return;
        }
        
        SunEvents_Set(location);
        cJSON_Delete(location);
        ACAP_HTTP_Respond_JSON(response, SunEventsSettings);
        return;
    }
    
	if (strcmp(method, "GET") == 0) {
		if (!SunEventsSettings) {
			LOG_WARN("%s: SunEventsSettings is NULL\n", __func__);
			ACAP_HTTP_Respond_Error(response, 500, "Sun Events not initialized");
			return;
		}
		
		// Debug print current settings
		char *debug_str = cJSON_PrintUnformatted(SunEventsSettings);
		if (debug_str) {
			LOG_TRACE("%s: Sending JSON response: %s\n", __func__, debug_str);
			free(debug_str);
		}
		
		ACAP_HTTP_Respond_JSON(response, SunEventsSettings);
		return;
	}
    
    ACAP_HTTP_Respond_Error(response, 405, "Method Not Allowed");
}

int SunEvents_Init() {
    LOG_TRACE("%s: Initializing sun events\n", __func__);
    
    SunEventsSettings = cJSON_CreateObject();
    if (!SunEventsSettings) return -1;
    
    // Load initial location from settings
    cJSON* settings = ACAP_Get_Config("settings");
    double lat = 51, lon = 0;
    
    if (settings) {
        cJSON* location = cJSON_GetObjectItem(settings, "geolocation");
        if (location) {
            lon = cJSON_GetObjectItem(location, "lon") ? 
                  cJSON_GetObjectItem(location, "lon")->valuedouble : 0;
            lat = cJSON_GetObjectItem(location, "lat") ? 
                  cJSON_GetObjectItem(location, "lat")->valuedouble : 0;
        }
    }

    SunEventsSettings = cJSON_CreateObject();
    if (!SunEventsSettings) {
        LOG_WARN("%s: Failed to create settings object\n", __func__);
        return -1;
    }

    // Initialize all required fields
    cJSON_AddNumberToObject(SunEventsSettings, "lat", lat);
    cJSON_AddNumberToObject(SunEventsSettings, "lon", lon);
    cJSON_AddNumberToObject(SunEventsSettings, "dawn", 0);
    cJSON_AddNumberToObject(SunEventsSettings, "sunrise", 0);
    cJSON_AddNumberToObject(SunEventsSettings, "sunnoon", 0);
    cJSON_AddNumberToObject(SunEventsSettings, "sunset", 0);
    cJSON_AddNumberToObject(SunEventsSettings, "dusk", 0);

    
    Calculate_Sun_Events(lat, lon);
    Setup_Midnight_Timer();
    ACAP_HTTP_Node("sunevents", HTTP_Endpoint_Sunevents);
    
    return 0;
}

static double to_rad(double deg) {
    return deg * M_PI / 180.0;
}

static double to_deg(double rad) {
    return rad * 180.0 / M_PI;
}

static void Calculate_Sun_Events(double lat, double lon) {
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) {
        LOG_WARN("%s: Invalid coordinates lat=%f, lon=%f\n", __func__, lat, lon);
        return;
    }

    LOG_TRACE("%s: Calculating sun events for lat=%f, lon=%f\n", __func__, lat, lon);

    if (!SunEventsSettings) {
        LOG_WARN("%s: SunEventsSettings is NULL\n", __func__);
        return;
    }
    
    time_t now;
    time(&now);
    struct tm* local = localtime(&now);
    
    // Calculate day of year
    int n = local->tm_yday + 1;
    
    // Calculate solar declination
    double declination = 23.45 * sin(to_rad((284 + n) * 360.0/365.0));
    
    // Convert latitude to radians
    double lat_rad = to_rad(lat);
    double decl_rad = to_rad(declination);
    
    // Calculate hour angle for sunrise/sunset
    double omega = acos(-tan(lat_rad) * tan(decl_rad));
    
    // Convert hour angle to hours and adjust for timezone
    double hours = to_deg(omega) / 15.0;
    double solar_noon = 12.0 - (lon / 15.0) + (local->tm_gmtoff / 3600.0);
    
    // Calculate times in seconds since midnight
    time_t midnight = now - (local->tm_hour * 3600) - (local->tm_min * 60) - local->tm_sec;
    time_t noon = midnight + (solar_noon * 3600);
	Setup_SunNoon_Timer(noon);
    
    // Calculate civil twilight adjustment (6 degrees below horizon)
    double twilight_adj = 0.4;
    
    // Calculate event times
    time_t sunrise = midnight + ((solar_noon - hours) * 3600);
    time_t sunset = midnight + ((solar_noon + hours) * 3600);
    time_t dawn = sunrise - (twilight_adj * 3600);
    time_t dusk = sunset + (twilight_adj * 3600);
    
    if (!SunEventsSettings) {
        LOG_WARN("%s: SunEventsSettings is NULL\n", __func__);
        return;
    }

    // Update JSON object with calculated times
    cJSON_ReplaceItemInObject(SunEventsSettings, "lat", cJSON_CreateNumber(lat));
    cJSON_ReplaceItemInObject(SunEventsSettings, "lon", cJSON_CreateNumber(lon));
    cJSON_ReplaceItemInObject(SunEventsSettings, "dawn", cJSON_CreateNumber(dawn));
    cJSON_ReplaceItemInObject(SunEventsSettings, "sunrise", cJSON_CreateNumber(sunrise));
    cJSON_ReplaceItemInObject(SunEventsSettings, "sunnoon", cJSON_CreateNumber(noon));
    cJSON_ReplaceItemInObject(SunEventsSettings, "sunset", cJSON_CreateNumber(sunset));
    cJSON_ReplaceItemInObject(SunEventsSettings, "dusk", cJSON_CreateNumber(dusk));
    
	LOG_TRACE("%s: Calculated times - Dawn: %lld, Sunrise: %lld, Noon: %lld, Sunset: %lld, Dusk: %lld\n",
          __func__, dawn, sunrise, noon, sunset, dusk);
}