#ifndef _timelapse_
#define _timelapse_

#include "cJSON.h"

#ifdef  __cplusplus
extern "C" {
#endif

typedef void (*Timelapse_Callback)(cJSON* profile);

int		Timelapse_Init( Timelapse_Callback callback );
int 	Timelapse_Save_Profiles();
cJSON* 	Timelapse_Get_Profiles();
cJSON*  Timelapse_Find_Profile_By_Id( const char *id );
cJSON*  Timelapse_Find_Profile_By_Name( const char *name );
cJSON*  Timelapse_Find_Profile_By_Event_Name( const char *name );
int		Timelapse_Remove_Profile_By_Id( const char* id );
void	Timelapse_Reset();

#ifdef  __cplusplus
}
#endif

#endif
