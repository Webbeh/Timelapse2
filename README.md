# Timelapse 2

Create timelapse videos with an Axis Camera. Typical use cases include:

- Construction site progress
- Monitoring seasonal changes or other slow-changing scenes
- Forensic search
- Event trigger validation

*If you find this ACAP valuable, please consider [buying me a coffee](https://buymeacoffee.com/fredjuhlinl).*

## [Download ACAP (zip)]()

---

## Overview

- **AXIS OS12 compliant**
- Replaces [TimelapseMe](https://pandosme.github.io/acap/2020/01/01/timelapse.html). Note: Migration from TimelapseMe to Timelapse 2 is not possible as they are two different ACAP applications.
- [Open-source project]()

---

## Key Features

- Supports multiple simultaneous timelapse recordings triggered by timers or events.
- Automatic archiving based on recording size, up to 1GB.
- Automatic removal of archived recordings with a configurable retention period to reduce the risk of exhausting the SD card.
- Geolocation-based sun events to filter image captures for daytime only.
- Fires a "Sun Noon" event that can capture images daily when the sun is at its peak height. Daylight savings is managed. This feature is recommended for construction site progress.

---

## Recordings
![recordings](images/recordings.jpg) 


A table of all active recordings.
  
### Actions

- **Download**: Download the AVI recording. A dialog will appear where you can select the playback frame rate (fps).
- **Inspect**: Open a dialog to view all captured images.
- **Archive**: Close the recording and move it to "Archive." The recording will continue capturing images.
- **Delete**: Remove all captures in the recording. The recording will continue capturing images.

---

## Settings
![settings](images/settings.jpg) 

  
Add and edit timelapse profiles.

### Actions

- **Edit**: Edit the profile. This will impact the active recording associated with the profile.  
  *Note: It is not recommended to change the resolution.*
- **Delete**: Remove the profile. The recording associated with the profile will **not** be removed.

---

## Location
![location](images/location.jpg)  

Set the geolocation of the camera. This calculates dawn, dusk, sunrise, sun noon, sunset, and dusk times. These sun events can be used to filter image captures during daytime only. Additionally, it fires a "Sun Noon" event that can capture an image when the sun is at its peak height.

Use the mouse to navigate the map and click on the location of the camera.

---

## Archive
![archive](images/archive.jpg)  
  
List and fetch closed recordings.

### Settings

- **Auto archive video when size exceeds**: Recordings larger than this size will automatically move to "Archived." It is recommended to keep a moderate size.
- **Auto remove archives older than**: Archived recordings older than this set duration will be automatically removed to reduce the risk of exhausting SD card storage. Specify the number of months you may need access to archived recordings.

---

# History

### 1.0.0 - December 25, 2024
- Initial release
