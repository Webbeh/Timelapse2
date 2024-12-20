#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <glib.h>
#include <glib-unix.h>
#include <signal.h>

#include "vdo-stream.h"
#include "vdo-frame.h"
#include "vdo-types.h"
#include "ACAP.h"
#include "cJSON.h"
#include "timelapse.h"
#include "recordings.h"
#include "sunevents.h"

#define APP_PACKAGE "timelapse2"

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_WARN(fmt, args...)    { syslog(LOG_WARNING, fmt, ## args); printf(fmt, ## args); }
#define LOG_TRACE(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
//#define LOG_TRACE(fmt, args...)    {}

void Trigger(cJSON* profile) {
    // Check if profile has conditions
    const char* conditions = cJSON_GetStringValue(cJSON_GetObjectItem(profile, "conditions"));
    if (conditions) {
        if (strcmp(conditions, "dawn_dusk") == 0 && !SunEvents_Between_Dawn_Dusk())
			return;
		if (strcmp(conditions, "sunrise_sunset") == 0 && !SunEvents_Between_Sunrise_Sunset())
			return;
    }

    // All conditions met or no conditions, capture the recording
    Recordings_Capture(profile);
}


void Settings_Updated_Callback(const char* service, cJSON* data) {
    char* json = cJSON_PrintUnformatted(data);
    LOG_TRACE("%s: Service=%s Data=%s\n", __func__, service, json);
    free(json);
	if( strcmp("settings",service) == 0 ) {
		cJSON* geolocation = cJSON_GetObjectItem(data,"geolocation");
		if( geolocation ) {
			ACAP_STATUS_SetBool("settings","geolocation",1);
			SunEvents_Set(geolocation);
		} else {
			LOG_WARN("%s: No geolocation in settingsn",__func__);
		}
	}
}

static GMainLoop *main_loop = NULL;

static gboolean
signal_handler(gpointer user_data) {
    LOG("Received SIGTERM, initiating shutdown\n");
    if (main_loop && g_main_loop_is_running(main_loop)) {
        g_main_loop_quit(main_loop);
    }
    return G_SOURCE_REMOVE;
}

int main(void) {
    openlog(APP_PACKAGE, LOG_PID | LOG_CONS, LOG_USER);
    LOG("------ Starting %s ------\n",APP_PACKAGE);
    ACAP_STATUS_SetString("app", "status", "The application is starting");


	// Create timelapse directory if it doesn't exist
	const char* timelapse_dir = "/var/spool/storage/SD_DISK/timelapse2";
	struct stat st = {0};
	if (stat(timelapse_dir, &st) == -1) {
		if (mkdir(timelapse_dir, 0755) == -1) {
			ACAP_STATUS_SetBool("sdcard", "status", 0);
			LOG_WARN("Failed to create directory %s\n", timelapse_dir);
		} else {
			ACAP_STATUS_SetBool("sdcard", "status", 1);
			LOG("Created directory %s\n", timelapse_dir);
		}
	}

    // Initialize ACAP and Timelapse
    ACAP(APP_PACKAGE, Settings_Updated_Callback);
    Timelapse_Init(Trigger);
	Recordings_Init();
    SunEvents_Init();
	
    // Create and run the main loop
	main_loop = g_main_loop_new(NULL, FALSE);
	GMainContext *context = g_main_loop_get_context(main_loop);

    LOG("Entering main loop\n");
	main_loop = g_main_loop_new(NULL, FALSE);
    GSource *signal_source = g_unix_signal_source_new(SIGTERM);
    if (signal_source) {
		g_source_set_callback(signal_source, signal_handler, NULL, NULL);
		g_source_attach(signal_source, NULL);
	} else {
		LOG_WARN("Signal detection failed");
	}
	
    g_main_loop_run(main_loop);
	LOG("------ Exit %s ------\n",APP_PACKAGE);
    ACAP_Cleanup();
    closelog();
    return 0;
}
