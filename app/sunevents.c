#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <math.h>
#include <glib.h>
#include "ACAP.h"
#include "cJSON.h"

#define LOG(fmt, args...) { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_WARN(fmt, args...) { syslog(LOG_WARNING, fmt, ## args); printf(fmt, ## args); }
#define LOG_TRACE(fmt, args...) { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
//#define LOG_TRACE(fmt, args...) {}

static cJSON* SunEventsSettings = NULL;
static GSource* timer_source = NULL;
static GSource* sunnoon_timer = NULL;
static time_t last_scheduled_noon = 0;

static void Calculate_Sun_Events(double lat, double lon);

static double to_rad(double deg) {
    return deg * M_PI / 180.0;
}

static double to_deg(double rad) {
    return rad * 180.0 / M_PI;
}

// Timer callback for solar noon
static gboolean SunNoon_Timer_Callback(gpointer user_data) {
    LOG_TRACE("%s: Sun noon event triggered\n", __func__);
    ACAP_EVENTS_Fire("sunnoon");
    return G_SOURCE_CONTINUE;
}

// Timer callback for midnight recalculation
static gboolean Timer_Callback(gpointer user_data) {
    LOG_TRACE("%s: Midnight timer triggered\n", __func__);
    double lat = cJSON_GetObjectItem(SunEventsSettings, "lat")->valuedouble;
    double lon = cJSON_GetObjectItem(SunEventsSettings, "lon")->valuedouble;
    Calculate_Sun_Events(lat, lon);
    return G_SOURCE_CONTINUE;
}

// Setup a timer for solar noon
static void Setup_SunNoon_Timer(time_t noon) {
    time_t now;
    time(&now);

    struct tm* tm_now = localtime(&now);
    struct tm* tm_noon = localtime(&noon);

    if (last_scheduled_noon == noon || 
        (tm_now->tm_yday == tm_noon->tm_yday && tm_now->tm_year == tm_noon->tm_year)) {
        LOG_TRACE("%s: Sun noon already scheduled for today\n", __func__);
        return;
    }

    last_scheduled_noon = noon;

    int seconds_to_noon = (int)(noon - now);
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
        return 0;
    }

	if( !ACAP_DEVICE_Set_Location( lat, lon) )
		LOG_WARN("%s: Error storing GeoLocation\n",__func__);
    
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
    return 1;
}

int SunEvents_Between_Dawn_Dusk() {
    if (!SunEventsSettings) {
        LOG_WARN("%s: SunEventsSettings is NULL\n", __func__);
        return 0;
    }

    char* json = cJSON_PrintUnformatted(SunEventsSettings);
    if (json) {
        LOG_TRACE("%s: %s\n", __func__, json);
        free(json);
    } else {
        LOG_TRACE("%s: Unable to print json\n", __func__);
    }

    cJSON* dawn_obj = cJSON_GetObjectItem(SunEventsSettings, "dawn");
    cJSON* dusk_obj = cJSON_GetObjectItem(SunEventsSettings, "dusk");
    if (!dawn_obj || !dusk_obj) {
        LOG_WARN("%s: Missing dawn or dusk in SunEventsSettings\n", __func__);
        return 0;
    }

    time_t now;
    time(&now);

    // Get dawn and dusk timestamps
    time_t dawn = (time_t)dawn_obj->valuedouble;
    time_t dusk = (time_t)dusk_obj->valuedouble;

    // Calculate seconds since midnight for now
    struct tm tm_now;
    localtime_r(&now, &tm_now); // Thread-safe version of localtime()
    time_t midnight_today = now - (tm_now.tm_hour * 3600 + tm_now.tm_min * 60 + tm_now.tm_sec);
    int now_seconds = now - midnight_today;

    // Calculate seconds since midnight for dawn
    struct tm tm_dawn;
    localtime_r(&dawn, &tm_dawn); // Thread-safe version of localtime()
    time_t midnight_dawn = dawn - (tm_dawn.tm_hour * 3600 + tm_dawn.tm_min * 60 + tm_dawn.tm_sec);
    int dawn_seconds = dawn - midnight_dawn;

    // Calculate seconds since midnight for dusk
    struct tm tm_dusk;
    localtime_r(&dusk, &tm_dusk); // Thread-safe version of localtime()
    time_t midnight_dusk = dusk - (tm_dusk.tm_hour * 3600 + tm_dusk.tm_min * 60 + tm_dusk.tm_sec);
    int dusk_seconds = dusk - midnight_dusk;

    LOG_TRACE("%s: now_seconds=%d, dawn_seconds=%d, dusk_seconds=%d\n",
              __func__, now_seconds, dawn_seconds, dusk_seconds);

    return (now_seconds >= dawn_seconds && now_seconds <= dusk_seconds) ? 1 : 0;
}

int SunEvents_Between_Sunrise_Sunset() {
    if (!SunEventsSettings) {
        LOG_WARN("%s: SunEventsSettings is NULL\n", __func__);
        return 0;
    }

    LOG_TRACE("%s: Debug 1\n", __func__);
    char* json = cJSON_PrintUnformatted(SunEventsSettings);
    if (json) {
        LOG_TRACE("%s: %s\n", __func__, json);
        free(json);
    } else {
        LOG_TRACE("%s: Unable to print json\n", __func__);
    }
    LOG_TRACE("%s: Debug 2\n", __func__);

    cJSON* sunrise_obj = cJSON_GetObjectItem(SunEventsSettings, "sunrise");
    cJSON* sunset_obj = cJSON_GetObjectItem(SunEventsSettings, "sunset");
    if (!sunrise_obj || !sunset_obj) {
        LOG_WARN("%s: Missing sunrise or sunset in SunEventsSettings\n", __func__);
        return 0;
    }

    time_t now;
    time(&now);

    // Get sunrise and sunset timestamps
    time_t sunrise = (time_t)sunrise_obj->valuedouble;
    time_t sunset = (time_t)sunset_obj->valuedouble;

    // Calculate seconds since midnight for now
    struct tm tm_now;
    localtime_r(&now, &tm_now); // Thread-safe version of localtime()
    time_t midnight_today = now - (tm_now.tm_hour * 3600 + tm_now.tm_min * 60 + tm_now.tm_sec);
    int now_seconds = now - midnight_today;

    // Calculate seconds since midnight for sunrise
    struct tm tm_sunrise;
    localtime_r(&sunrise, &tm_sunrise); // Thread-safe version of localtime()
    time_t midnight_sunrise = sunrise - (tm_sunrise.tm_hour * 3600 + tm_sunrise.tm_min * 60 + tm_sunrise.tm_sec);
    int sunrise_seconds = sunrise - midnight_sunrise;

    // Calculate seconds since midnight for sunset
    struct tm tm_sunset;
    localtime_r(&sunset, &tm_sunset); // Thread-safe version of localtime()
    time_t midnight_sunset = sunset - (tm_sunset.tm_hour * 3600 + tm_sunset.tm_min * 60 + tm_sunset.tm_sec);
    int sunset_seconds = sunset - midnight_sunset;

    LOG_TRACE("%s: now_seconds=%d, sunrise_seconds=%d, sunset_seconds=%d\n",
              __func__, now_seconds, sunrise_seconds, sunset_seconds);

    return (now_seconds >= sunrise_seconds && now_seconds <= sunset_seconds) ? 1 : 0;
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
    
    double lat = ACAP_DEVICE_Latitude();
	double lon = ACAP_DEVICE_Longitude();

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

static void Calculate_Sun_Events(double lat, double lon) {
	if (lat < -90 || lat > 90 || lon < -180 || lon > 180) {
		LOG_WARN("%s: Invalid coordinates lat=%f, lon=%f\n", __func__, lat, lon);
		return;
	}

	LOG_TRACE("%s: Calculating sun events for lat=%f, lon=%f\n", __func__, lat, lon);

	time_t now;
	time(&now);

	struct tm* local = localtime(&now);
	if (!local) {
		LOG_WARN("%s: Failed to get local time\n", __func__);
		return;
	}

	// Day of year
	int n = local->tm_yday + 1;

	// Solar declination
	double declination = 23.45 * sin(to_rad((360 / 365.0) * (n - 81)));

	// Equation of time
	double B = to_rad((360 / 365.0) * (n - 81));
	double equation_of_time = 9.87 * sin(2 * B) - 7.53 * cos(B) - 1.5 * sin(B);

	// Solar noon in UTC
	double solar_noon_utc = (12.0 - (lon / 15.0)) - (equation_of_time / 60.0);

	// Hour angle for sunrise/sunset
	double lat_rad = to_rad(lat);
	double decl_rad = to_rad(declination);
	double ha_sunrise = acos(cos(to_rad(90.833)) / (cos(lat_rad) * cos(decl_rad)) -
							 tan(lat_rad) * tan(decl_rad));

	// Convert hour angle to hours
	double ha_hours_sunrise = to_deg(ha_sunrise) / 15.0;

	// Calculate sunrise and sunset in UTC
	double sunrise_utc = solar_noon_utc - ha_hours_sunrise;
	double sunset_utc = solar_noon_utc + ha_hours_sunrise;

	// Hour angle for civil twilight (dawn/dusk)
	double ha_twilight = acos(cos(to_rad(96)) / (cos(lat_rad) * cos(decl_rad)) -
							  tan(lat_rad) * tan(decl_rad));

	// Convert hour angle to hours
	double ha_hours_twilight = to_deg(ha_twilight) / 15.0;

	// Calculate dawn and dusk in UTC
	double dawn_utc = solar_noon_utc - ha_hours_twilight;
	double dusk_utc = solar_noon_utc + ha_hours_twilight;

	// Convert UTC times to local time
	int timezone_offset_seconds = local->tm_gmtoff; // Offset in seconds from UTC
	time_t midnight = now - (local->tm_hour * 3600 + local->tm_min * 60 + local->tm_sec);

	time_t dawn = midnight + (time_t)(dawn_utc * 3600) + timezone_offset_seconds;
	time_t sunrise = midnight + (time_t)(sunrise_utc * 3600) + timezone_offset_seconds;
	time_t solar_noon = midnight + (time_t)(solar_noon_utc * 3600) + timezone_offset_seconds;
	time_t sunset = midnight + (time_t)(sunset_utc * 3600) + timezone_offset_seconds;
	time_t dusk = midnight + (time_t)(dusk_utc * 3600) + timezone_offset_seconds;

	LOG_TRACE("%s: Calculated times - Dawn: %lld, Sunrise: %lld, Noon: %lld, Sunset: %lld, Dusk: %lld\n",
			  __func__, (long long)dawn, (long long)sunrise,
			  (long long)solar_noon, (long long)sunset,
			  (long long)dusk);

	// Validate calculated times
	if (dawn < midnight || dawn > midnight + 24 * 3600 ||
	   sunrise < midnight || sunrise > midnight + 24 * 3600 ||
	   solar_noon < midnight || solar_noon > midnight + 24 * 3600 ||
	   sunset < midnight || sunset > midnight + 24 * 3600 ||
	   dusk < midnight || dusk > midnight + 24 * 3600) {
	   LOG_WARN("%s: One or more calculated times are out of range\n", __func__);
	   return;
	}

	// Update JSON object with calculated values
	cJSON_ReplaceItemInObject(SunEventsSettings, "lat", cJSON_CreateNumber(lat));
	cJSON_ReplaceItemInObject(SunEventsSettings, "lon", cJSON_CreateNumber(lon));
	cJSON_ReplaceItemInObject(SunEventsSettings, "dawn", cJSON_CreateNumber((double)dawn));
	cJSON_ReplaceItemInObject(SunEventsSettings, "sunrise", cJSON_CreateNumber((double)sunrise));
	cJSON_ReplaceItemInObject(SunEventsSettings, "sunnoon", cJSON_CreateNumber((double)solar_noon));
	cJSON_ReplaceItemInObject(SunEventsSettings, "sunset", cJSON_CreateNumber((double)sunset));
	cJSON_ReplaceItemInObject(SunEventsSettings, "dusk", cJSON_CreateNumber((double)dusk));
   
	char* json = cJSON_PrintUnformatted(SunEventsSettings);
	if(json) {
		LOG_TRACE("%s: %s\n",__func__,json);
		free(json);
	}
	
    // Setup timer for solar noon
    Setup_SunNoon_Timer(solar_noon);
}
