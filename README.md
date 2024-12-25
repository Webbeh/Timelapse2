# Timelapse 2

Make timelapse videos with an Axis Camera.  The typical use cases are
* Construction site progress
* Monitoring seasonal changes or other slow changing scenes
* Forensic search
* Event trigger validation

*If you find this ACAP valuable, please consider (buying me a cup of coffe)[https://buymeacoffee.com/fredjuhlinl]*

## [Download ACAP (zip)]()

### Overview
* AXIS OS12 complient
* Replaces [TimlapseMe](https://pandosme.github.io/acap/2020/01/01/timelapse.html).  Note that it is not possible to migrate TimelapseMe to Timelapse2 as thay are two different ACAP applications.
* (Open-source project)[https://github.com/pandosme/Timelapse2]


### Key Features
* Supports multiple simultaniosly timelapse recordings triggered by either timers or events.
* Automatic archiving nased on recording size up to 1GB
* Automatic removal of archived recordings. The retention period is configurable.  The reason ro retention is to reduce the risk of exhausting the SD Card.
* Geolocation for sun events that can filter image captures for daytime only.
* Fires an event "Sun Noon" that can be used capture images every day when the sun is in peak height.  Daylight svaings is managed.  This event is recommended for construction site progress.

## Recordings
A table of all active recordings.  

![recordings](images/recordings.jpg)

### Actions
Download:  Download to play the AVI recording.  A dialog will be sown and you can select the playback frame rate (fps)  
Inspect: A dialog will open to view all captured images  
Archive: Close the recording and move it to "Archive".  The recording will contionue capturing images.  
Delete: Remove all the capture in the recording. The recording will contionue capturing images.  

## Settings
Add and edit timelapse profiles.  

![settings](images/settings.jpg)

### Actions
Edit:  Edit the profile.  This will impact the active recording assiciated to the profile.  *Note: It is not recommended to change the resolution.*  
Delete: Removes the profile.  The recording associated with the profile will _NOT_ be remvoved.  

## Location
Set the GeoLocation of the camera.  This will calculate dawn, dusk, sunrise, sun noon, sunset and dusk.  These sun events may used to filter image capturing only during daytime.  It also fires an event "sunnoon" that can be used to capture an image when the sun is at peak.
![location](images/location.jpg)

Use the mouse to navigate the map and click on the location of the camera.

## Archive
List and fetch the closed recordings.
![archive](images/archive.jpg)

### Settings
Auto archive video when reached:  Recordings larger that this size will be automatically move to archived.  It is recommeded to keep a moderate size.  
Auto remove archives older than: To reduce risk of exhausting the SD Card, archived will be automatically removed.  Set the number of months you may need to download an archived recording.

# History

### 1.0.0	December 25, 2024
- Initial commit
