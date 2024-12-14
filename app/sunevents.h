#ifndef _sunevents_
#define _sunevents_

#include "cJSON.h"

#ifdef  __cplusplus
extern "C" {
#endif

int		SunEvents_Init();
int		SunEvents_Set(cJSON* location);
int		SunEvents_Between_Dawn_Dusk();
int		SunEvents_Between_Sunrise_Sunset();

#ifdef  __cplusplus
}
#endif

#endif
