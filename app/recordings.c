#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <syslog.h>
#include "vdo-stream.h"
#include "vdo-frame.h"
#include "vdo-types.h"
#include "ACAP.h"
#include "cJSON.h"
#include "recordings.h"

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_WARN(fmt, args...)    { syslog(LOG_WARNING, fmt, ## args); printf(fmt, ## args); }
#define LOG_TRACE(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
//#define LOG_TRACE(fmt, args...)    {}

#define PATH_MAX_LEN 1024
#define RIFF_HEADER_SIZE 44
#define AVI_ALIGN_SIZE 2048

typedef unsigned int DWORD;

#if __BYTE_ORDER == __BIG_ENDIAN
    #define LILEND4(a) SWAP4((a))
#else
    #define LILEND4(a) (a)
#endif

#define SWAP4(x) (((x>>24) & 0x000000ff) | \
                  ((x>>8)  & 0x0000ff00) | \
                  ((x<<8)  & 0x00ff0000) | \
                  ((x<<24) & 0xff000000))

#define AVIF_HASINDEX 0x00000010

struct AVI_HEADER_STRUCT {
    DWORD LIST_RIFF;      // "RIFF"
    DWORD RIFF_size;      // 
    DWORD RIFF_FOURCC;    // "AVI "
    DWORD LIST_HDRL;      // "LIST"
    DWORD hdrl_size;      // 208
    DWORD hdrl_name;      // "hdrl"
    DWORD avih;           // "avih"
    DWORD avih_size;      // 56
    DWORD AVIH_MicroSecPerFrame;
    DWORD AVIH_MaxBytesPerSec;
    DWORD AVIH_PaddingGranularity;
    DWORD AVIH_Flags;
    DWORD AVIH_TotalFrames;
    DWORD AVIH_InitialFrames;
    DWORD AVIH_Streams;
    DWORD AVIH_SugestedBufferSize;
    DWORD AVIH_Width;
    DWORD AVIH_Height;
    DWORD AVIH_Reserved1;
    DWORD AVIH_Reserved2;
    DWORD AVIH_Reserved3;
    DWORD AVIH_Reserved4;
    DWORD LIST_strl;      // "LIST"
    DWORD LIST_strl_size; // 132
    DWORD LIST_strl_name; // "strl"
    DWORD STRH_name;      // "strh"
    DWORD STRH_size;      // 48
    DWORD strh_fccType;
    DWORD strh_fccHandler;
    DWORD strh_flags;
    DWORD strh_priority;
    DWORD strh_init_frames;
    DWORD strh_scale;
    DWORD strh_rate;
    DWORD strh_start;
    DWORD strh_length;
    DWORD strh_sugg_buff_sz;
    DWORD strh_quality;
    DWORD strh_sample_sz;
    DWORD LIST_strf;      // "strf"
    DWORD strf_size_list; // 40
    DWORD strf_size;      // 40
    DWORD strf_width;
    DWORD strf_height;
    DWORD strf_planes_bit_cnt;
    DWORD strf_compression;
    DWORD strf_image_size;
    DWORD strf_xpels_meter;
    DWORD strf_ypels_meter;
    DWORD strf_num_colors;
    DWORD strf_imp_colors;
    DWORD LIST_ODML;      // "LIST"
    DWORD LIST_ODML_Size; // 16
    DWORD LIST_ODML_type; // "odml"
    DWORD odml_fourCC;    // "dmlh"
    DWORD odml_size;      // 4
    DWORD odml_frames;
    DWORD LIST_movi;      // "LIST"
    DWORD LIST_movi_size; // SUM(JPEG data size) + (8 * frames) + 4
    DWORD LIST_movi_name; // "movi"
};
typedef struct AVI_HEADER_STRUCT AVI_HEADER;

struct AVI_INDEX_ENTRY_STRUCT {
    DWORD fourCC;    // "00dc"
    DWORD flags;     // Usually 0
    DWORD offset;    // Offset from movi start
    DWORD size;      // Size of frame
};
typedef struct AVI_INDEX_ENTRY_STRUCT AVI_INDEX_ENTRY;

struct LIST_INDEX_STRUCT {
    DWORD fourCC;
    DWORD size;
};
typedef struct LIST_INDEX_STRUCT LIST_INDEX;

struct AVIOLDINDEX_STRUCT {
    DWORD fourCC;    // 'idx1'
    DWORD cb;        // Size not including first 8 bytes
};
typedef struct AVIOLDINDEX_STRUCT AVIOLDINDEX;

static cJSON* Recordings_Container = NULL;

static DWORD FOURCC(const char* str) {
    DWORD value = 0;
    value = str[3];
    value <<= 8;
    value += str[2];
    value <<= 8;
    value += str[1];
    value <<= 8;
    value += str[0];
    return value;
}


static void write_avi_header(FILE* f, DWORD frames, DWORD totalJPEGSize, DWORD width, DWORD height, unsigned int fps);
static size_t write_avi_frame(FILE* f, const unsigned char* data, size_t size);
static int avi_add_index_entry(FILE* file, unsigned int frame, unsigned int jpeg_size);
static void ensure_profile_directory(const char* profileId);
static cJSON* load_recordings(void);
static void save_recordings(void);

static void write_avi_header(FILE* f, DWORD frames, DWORD totalJPEGSize, DWORD width, DWORD height, unsigned int fps) {
    AVI_HEADER header;
    DWORD riffsize;

    if (!fps) fps = 30;

    header.LIST_RIFF = FOURCC("RIFF");
    riffsize = sizeof(AVI_HEADER);  // Instead of 220
    riffsize += sizeof(LIST_INDEX) + totalJPEGSize + (sizeof(LIST_INDEX) * frames); // movi
    riffsize += sizeof(LIST_INDEX) + (sizeof(AVI_INDEX_ENTRY) * frames); // index
    header.RIFF_size = LILEND4(riffsize);
    header.RIFF_FOURCC = FOURCC("AVI ");
    header.LIST_HDRL = FOURCC("LIST");
    header.hdrl_size = LILEND4(208);
    header.hdrl_name = FOURCC("hdrl");
    header.avih = FOURCC("avih");
    header.avih_size = LILEND4(56);
    header.AVIH_MicroSecPerFrame = LILEND4(1000000/fps);
    header.AVIH_MaxBytesPerSec = LILEND4(width * height * 3 * fps);
    header.AVIH_PaddingGranularity = LILEND4(0);
    header.AVIH_Flags = LILEND4(AVIF_HASINDEX);
    header.AVIH_TotalFrames = LILEND4(frames);
    header.AVIH_InitialFrames = LILEND4(0);
    header.AVIH_Streams = LILEND4(1);
    header.AVIH_SugestedBufferSize = LILEND4(width * height * 3);
    header.AVIH_Width = LILEND4(width);
    header.AVIH_Height = LILEND4(height);
    header.AVIH_Reserved1 = LILEND4(0);
    header.AVIH_Reserved2 = LILEND4(0);
    header.AVIH_Reserved3 = LILEND4(0);
    header.AVIH_Reserved4 = LILEND4(0);

    // Stream LIST
    header.LIST_strl = FOURCC("LIST");
    header.LIST_strl_size = LILEND4(132);
    header.LIST_strl_name = FOURCC("strl");
    header.STRH_name = FOURCC("strh");
    header.STRH_size = LILEND4(48);
    header.strh_fccType = FOURCC("vids");
    header.strh_fccHandler = FOURCC("MJPG");
    header.strh_flags = LILEND4(0);
    header.strh_priority = LILEND4(0);
    header.strh_init_frames = LILEND4(0);
    header.strh_scale = LILEND4(1);
    header.strh_rate = LILEND4(fps);
    header.strh_start = LILEND4(0);
    header.strh_length = LILEND4(frames);
    header.strh_sugg_buff_sz = LILEND4(width * height * 3);
    header.strh_quality = LILEND4(0);
    header.strh_sample_sz = LILEND4(0);

    // Stream format
    header.LIST_strf = FOURCC("strf");
    header.strf_size_list = LILEND4(40);
    header.strf_size = LILEND4(40);
    header.strf_width = LILEND4(width);
    header.strf_height = LILEND4(height);
    header.strf_planes_bit_cnt = LILEND4(1 | (24<<16));  // 1 plane, 24 bits
    header.strf_compression = FOURCC("MJPG");
    header.strf_image_size = LILEND4(width * height * 3);
    header.strf_xpels_meter = LILEND4(0);
    header.strf_ypels_meter = LILEND4(0);
    header.strf_num_colors = LILEND4(0);
    header.strf_imp_colors = LILEND4(0);

    // ODML
    header.LIST_ODML = FOURCC("LIST");
    header.LIST_ODML_Size = LILEND4(16);
    header.LIST_ODML_type = FOURCC("odml");
    header.odml_fourCC = FOURCC("dmlh");
    header.odml_size = LILEND4(4);
    header.odml_frames = LILEND4(frames);

    // Movie data
	header.LIST_movi = FOURCC("LIST");
	header.LIST_movi_size = LILEND4(4 + totalJPEGSize + (frames * sizeof(LIST_INDEX)));
    header.LIST_movi_name = FOURCC("movi");
	fseek(f, 0, SEEK_SET);
    fwrite(&header, sizeof(AVI_HEADER), 1, f);
}


static int avi_initialize_index(FILE* file) {
    AVIOLDINDEX header;
    
    if (!file) return 0;
    
    header.fourCC = FOURCC("idx1");
    header.cb = LILEND4(0);  // Initial size is 0
	fseek(file, 0, SEEK_SET);    
    return fwrite(&header, sizeof(AVIOLDINDEX), 1, file) == 1;
}

static int avi_add_index_entry(FILE* file, unsigned int frames, unsigned int jpeg_size) {
    AVI_INDEX_ENTRY index_entry;
    AVI_INDEX_ENTRY prev_entry;
    AVIOLDINDEX header;
    size_t offset;

    if (!file) return 0;

    // Update index header with correct size
    fseek(file, 0, SEEK_SET);
    header.fourCC = FOURCC("idx1");
    header.cb = LILEND4(frames * sizeof(AVI_INDEX_ENTRY));
    fwrite(&header, sizeof(AVIOLDINDEX), 1, file);

    // Calculate offset based on previous frame
    if (frames > 1) {
        fseek(file, -sizeof(AVI_INDEX_ENTRY), SEEK_END);
        fread(&prev_entry, sizeof(AVI_INDEX_ENTRY), 1, file);
        offset = LILEND4(prev_entry.offset) + LILEND4(prev_entry.size) + sizeof(LIST_INDEX);
    } else {
        offset = 4;  // First frame starts after "movi" tag
    }

    // Create index entry
    index_entry.fourCC = FOURCC("00db");
    index_entry.flags = LILEND4(0);
    index_entry.offset = LILEND4(offset);
    index_entry.size = LILEND4(jpeg_size);

    fseek(file, 0, SEEK_END);
    return fwrite(&index_entry, sizeof(AVI_INDEX_ENTRY), 1, file) == 1;
}

static size_t write_avi_frame(FILE* f, const unsigned char* data, size_t size) {
    // Calculate padding needed for 4-byte alignment
	fseek(f, 0, SEEK_END);	
    unsigned int padding = (4-(size%4)) % 4;
    size_t total_size = size + padding;
    
    LIST_INDEX lindex;
    lindex.fourCC = FOURCC("00db");
    lindex.size = LILEND4(size);
    
    fwrite(&lindex, sizeof(LIST_INDEX), 1, f);
    fwrite((void*)data, 1, size, f);
    
    // Write padding if needed
    if (padding > 0) {
        char pad[4] = {0};
        fwrite(pad, 1, padding, f);
    }
    
    return total_size;
}

int Recordings_Clear(const char* profileId) {
    LOG_TRACE("%s:\n", __func__);
    char path[128];
    sprintf(path, "/var/spool/storage/SD_DISK/timelapse2/%s", profileId);
    
    DIR* dir = opendir(path);
    if (!dir) return -1;

    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || 
            strcmp(entry->d_name, "..") == 0 || 
            strcmp(entry->d_name, "timelapse.avi") == 0 || 
            strcmp(entry->d_name, "timelapse.idx") == 0)
            continue;

        char filepath[PATH_MAX_LEN];
        sprintf(filepath, "%s/%s", path, entry->d_name);
        unlink(filepath);
    }
    closedir(dir);

    // Reset AVI file
    char avifile[PATH_MAX_LEN];
    sprintf(avifile, "%s/timelapse.avi", path);
    FILE* f = fopen(avifile, "w");
    if (f) fclose(f);

    // Reset index file
    char idxfile[PATH_MAX_LEN];
    snprintf(idxfile, sizeof(idxfile), "%s/timelapse.idx", path);
    f = fopen(idxfile, "w");
    if (f) fclose(f);

    // Update recordings metadata
    if (!Recordings_Container) {
        load_recordings();
    }
    cJSON* recording = cJSON_GetObjectItem(Recordings_Container, profileId);
    if (recording) {
        cJSON_SetNumberValue(cJSON_GetObjectItem(recording, "images"), 0);
        cJSON_SetNumberValue(cJSON_GetObjectItem(recording, "first"), 0);
        cJSON_SetNumberValue(cJSON_GetObjectItem(recording, "last"), 0);
        save_recordings();
    }
    return 0;
}

cJSON* Recordings_Get_List(void) {
    if (!Recordings_Container) {
        load_recordings();
    }
    return Recordings_Container;
}

cJSON* Recordings_Get_Metadata(const char* profileId) {
    if (!Recordings_Container) {
        load_recordings();
    }
    return cJSON_GetObjectItem(Recordings_Container, profileId);
}

static void ensure_profile_directory(const char* profileId) {
    char path[PATH_MAX_LEN];
    sprintf(path, "/var/spool/storage/SD_DISK/timelapse2/%s", profileId);
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        mkdir(path, 0755);
    }
}

static cJSON* load_recordings(void) {
    char path[PATH_MAX_LEN];
    sprintf(path, "/var/spool/storage/SD_DISK/timelapse2/recordings.json");
    
    FILE* file = fopen(path, "r");
    if (!file) {
        return cJSON_CreateObject();
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* json = malloc(size + 1);
    if (!json) {
        fclose(file);
        return cJSON_CreateObject();
    }
    
    fread(json, 1, size, file);
    json[size] = 0;
    fclose(file);
    
    cJSON* recordings = cJSON_Parse(json);
    free(json);
    
    return recordings ? recordings : cJSON_CreateObject();
}

static void save_recordings(void) {
    char path[PATH_MAX_LEN];
    sprintf(path, "/var/spool/storage/SD_DISK/timelapse2/recordings.json");
    
    char* json = cJSON_PrintUnformatted(Recordings_Container);
    if (!json) return;
    
    FILE* file = fopen(path, "w");
    if (file) {
        fwrite(json, strlen(json), 1, file);
        fclose(file);
    }
    free(json);
}

int Recordings_Capture(cJSON* profile) {
    if (!profile) return -1;
    const char* profileId = cJSON_GetObjectItem(profile, "id")->valuestring;
    const char* resolution = cJSON_GetObjectItem(profile, "resolution")->valuestring;
    if (!profileId || !resolution) return -1;


	LOG_TRACE("%s: ID=%s Resolution=%s\n",__func__,profileId,resolution);

    // Parse resolution
    char* width_str = strdup(resolution);
    char* height_str = strchr(width_str, 'x');
    if (height_str) {
        *height_str = '\0';
        height_str++;
    }
    int width = width_str ? atoi(width_str) : 1920;
    int height = height_str ? atoi(height_str) : 1080;
    free(width_str);

    // Capture image using VDO
    VdoMap* vdoSettings = vdo_map_new();
    vdo_map_set_uint32(vdoSettings, "format", VDO_FORMAT_JPEG);
    vdo_map_set_uint32(vdoSettings, "width", width);
    vdo_map_set_uint32(vdoSettings, "height", height);
    if (cJSON_GetObjectItem(profile, "overlay") && 
        cJSON_GetObjectItem(profile, "overlay")->type == cJSON_True) {
        vdo_map_set_string(vdoSettings, "overlays", "all,sync");
    }

    GError* error = NULL;
    VdoBuffer* buffer = vdo_stream_snapshot(vdoSettings, &error);
    g_clear_object(&vdoSettings);
    if (error != NULL) {
        LOG_WARN("%s: Snapshot capture failed: %s\n", __func__, error->message);
        g_error_free(error);
        return -1;
    }

    // Get image data
    unsigned char* jpegData = vdo_buffer_get_data(buffer);
    unsigned int jpegSize = vdo_frame_get_size(buffer);

	if(!jpegData || ! jpegSize ) {
		LOG_WARN("%s: Invalid capture data\n",__func__);
		return -1;
	}

    // Ensure directory exists
    ensure_profile_directory(profileId);

    double timestamp = ACAP_DEVICE_Timestamp();

    // Load metadata to get current frame count and total size
    if (!Recordings_Container) {
        load_recordings();
    }

    unsigned int frames = 0;
    unsigned int fps = 10;
    DWORD totalJPEGSize = 0;

    cJSON* recording = cJSON_GetObjectItem(Recordings_Container, profileId);

    if (!recording) {
        recording = cJSON_CreateObject();
        cJSON_AddItemToObject(Recordings_Container, profileId, recording);
        cJSON_AddNumberToObject(recording, "images", 0);
        cJSON_AddNumberToObject(recording, "size", 0);
        cJSON_AddNumberToObject(recording, "first", timestamp);
        cJSON_AddNumberToObject(recording, "last", 0);
        cJSON_AddNumberToObject(recording, "fps", 10);
    } else {
        frames = cJSON_GetObjectItem(recording, "images")->valueint;
        fps = cJSON_GetObjectItem(recording, "fps")?cJSON_GetObjectItem(recording, "fps")->valueint:10;
        totalJPEGSize = cJSON_GetObjectItem(recording, "size")->valueint;
    }

    // Open or create AVI file
    char filepath[PATH_MAX_LEN];
    sprintf(filepath, "/var/spool/storage/SD_DISK/timelapse2/%s/timelapse.avi", profileId);
    FILE* aviFile = fopen(filepath, "rb+");
    if (!aviFile) {
        aviFile = fopen(filepath, "wb+");
		write_avi_header(aviFile, 1, jpegSize, width, height, fps);
	}

    // Open or create index file
    sprintf(filepath, "/var/spool/storage/SD_DISK/timelapse2/%s/timelapse.idx", profileId);
    FILE* indexFile = fopen(filepath, "rb+");
    if (!indexFile) {
        indexFile = fopen(filepath, "wb+");
        if (indexFile) {
            avi_initialize_index(indexFile);
        }
    }

    if (!aviFile || !indexFile) {
        if (aviFile) fclose(aviFile);
        if (indexFile) fclose(indexFile);
        g_object_unref(buffer);
        return -1;
    }

    size_t frameSize = write_avi_frame(aviFile, jpegData, jpegSize);
	totalJPEGSize += frameSize;
	frames++;
    avi_add_index_entry(indexFile, frames, frameSize);
	write_avi_header(aviFile, frames, totalJPEGSize, width, height, fps);

	cJSON_SetNumberValue(cJSON_GetObjectItem(recording, "last"), timestamp);
	cJSON_SetNumberValue(cJSON_GetObjectItem(recording, "images"), frames);
	cJSON_SetNumberValue(cJSON_GetObjectItem(recording, "size"), totalJPEGSize);

    // Cleanup
    fclose(aviFile);
    fclose(indexFile);
    g_object_unref(buffer);

    // Update recordings metadata
    save_recordings();
    return 0;
}

static void
HTTP_Endpoint_Image(const ACAP_HTTP_Response response, 
                              const ACAP_HTTP_Request request) {
    const char* method = ACAP_HTTP_Get_Method(request);
    LOG_TRACE("%s: %s\n", __func__, method);
    
    if (strcmp(method, "GET") != 0) {
        ACAP_HTTP_Respond_Error(response, 405, "Method not allowed");
        return;
    }

    const char* profileId = ACAP_HTTP_Request_Param(request, "id");
    const char* indexStr = ACAP_HTTP_Request_Param(request, "index");

    LOG_TRACE("%s: %s %s\n", __func__, profileId, indexStr);

    if (!profileId || !indexStr) {
        ACAP_HTTP_Respond_Error(response, 400, "Missing parameters");
        return;
    }

    int index = atoi(indexStr);
    
    // Read frame index
    char idxfile[PATH_MAX_LEN];
    sprintf(idxfile, "/var/spool/storage/SD_DISK/timelapse2/%s/timelapse.idx", profileId);
    
    FILE* idxf = fopen(idxfile, "rb");
    if (!idxf) {
        ACAP_HTTP_Respond_Error(response, 404, "Recording not found");
        return;
    }

    // Read index header and entry
    LIST_INDEX list_header;
    AVI_INDEX_ENTRY entry;
    
    // Read index header
    if (fread(&list_header, sizeof(LIST_INDEX), 1, idxf) != 1) {
        fclose(idxf);
        ACAP_HTTP_Respond_Error(response, 404, "Invalid index file");
        return;
    }
    
    // Seek to frame entry
    fseek(idxf, sizeof(LIST_INDEX) + ((index-1) * sizeof(AVI_INDEX_ENTRY)), SEEK_SET);
    if (fread(&entry, sizeof(AVI_INDEX_ENTRY), 1, idxf) != 1) {
        fclose(idxf);
        ACAP_HTTP_Respond_Error(response, 404, "Frame not found");
        return;
    }
    fclose(idxf);

    LOG_TRACE("%s: Index Entry: offset=%u, size=%u\n", __func__, 
              LILEND4(entry.offset), LILEND4(entry.size));

    // Read frame from AVI
    char avifile[PATH_MAX_LEN];
    sprintf(avifile, "/var/spool/storage/SD_DISK/timelapse2/%s/timelapse.avi", profileId);
    
    FILE* avif = fopen(avifile, "rb");
    if (!avif) {
        ACAP_HTTP_Respond_Error(response, 404, "Recording file not found");
        return;
    }

    // Calculate correct offset: AVI header + movi list header + frame offset
	long frame_offset = sizeof(AVI_HEADER) + LILEND4(entry.offset) - 4;
	LOG_TRACE("%s: File offset = %ld\n",__func__,frame_offset);
	fseek(avif, frame_offset, SEEK_SET);

	// Skip chunk header (8 bytes: '00db' + size)
	fseek(avif, sizeof(LIST_INDEX), SEEK_CUR);

	// Allocate buffer for actual JPEG data
	unsigned int frame_size = LILEND4(entry.size);
	char* buffer = malloc(frame_size);
	if (!buffer) {
		fclose(avif);
		ACAP_HTTP_Respond_Error(response, 500, "Memory allocation failed");
		return;
	}

	fread(buffer, 1, frame_size, avif);
    fclose(avif);

    ACAP_HTTP_Respond_String(response, "status: 200 OK\r\n");
    ACAP_HTTP_Respond_String(response, "Content-Type: image/jpeg\r\n");
    ACAP_HTTP_Respond_String(response, "Content-Length: %u\r\n", frame_size);
    ACAP_HTTP_Respond_String(response, "\r\n");

    int result = ACAP_HTTP_Respond_Data(response, frame_size, buffer);
    if (result != 1) {
        LOG_WARN("%s: Failed to send image data\n", __func__);
    }

    free(buffer);
}

static void update_avi_fps(FILE* f, unsigned int fps) {
	LOG_TRACE("%s: Updating FPS=%d\n",__func__,fps);
    // Update microseconds per frame
    fseek(f, offsetof(AVI_HEADER, AVIH_MicroSecPerFrame), SEEK_SET);
    DWORD usec = LILEND4(1000000/fps);
    fwrite(&usec, sizeof(DWORD), 1, f);
    
    // Update rate in stream header
    fseek(f, offsetof(AVI_HEADER, strh_rate), SEEK_SET);
    DWORD rate = LILEND4(fps);
    fwrite(&rate, sizeof(DWORD), 1, f);
}


static void HTTP_Endpoint_Export(const ACAP_HTTP_Response response, 
                               const ACAP_HTTP_Request request) {
    const char* method = ACAP_HTTP_Get_Method(request);
	LOG_TRACE("%s: %s\n", __func__,method);
    if (strcmp(method, "GET") != 0) {
        ACAP_HTTP_Respond_Error(response, 405, "Method not allowed");
        return;
    }

    const char* profileId = ACAP_HTTP_Request_Param(request, "id");
    const char* fpsString = ACAP_HTTP_Request_Param(request, "fps");
    const char* filename = ACAP_HTTP_Request_Param(request, "filename");


    
    if (!profileId || !fpsString || !filename) {
        ACAP_HTTP_Respond_Error(response, 400, "Missing parameters");
        return;
    }
	LOG_TRACE("%s: %s %s %s\n", __func__, profileId, filename, fpsString );

	int fps = atoi(fpsString);
	if( fps < 1 ) fps = 1;
	if (fps > 60) fps = 60;

    char avipath[PATH_MAX_LEN], idxpath[PATH_MAX_LEN];
    snprintf(avipath, sizeof(avipath), 
             "/var/spool/storage/SD_DISK/timelapse2/%s/timelapse.avi", profileId);
    snprintf(idxpath, sizeof(idxpath), 
             "/var/spool/storage/SD_DISK/timelapse2/%s/timelapse.idx", profileId);

	FILE* aviFile = fopen(avipath, "rb+");
    FILE* idxFile = fopen(idxpath, "rb");
    
    if (!aviFile || !idxFile) {
        if (aviFile) fclose(aviFile);
        if (idxFile) fclose(idxFile);
        ACAP_HTTP_Respond_Error(response, 404, "Recording not found");
        return;
    }

    cJSON* recording = cJSON_GetObjectItem(Recordings_Container, profileId);
	if( recording ) {
		if( !cJSON_GetObjectItem(recording,"fps") ) {
			cJSON_AddNumberToObject(recording,"fps",fps );
			update_avi_fps(aviFile, fps);
			save_recordings();
		}
		if( cJSON_GetObjectItem(recording,"fps")->valueint != fps ) {
			cJSON_SetNumberValue(cJSON_GetObjectItem(recording, "fps"), fps);
			update_avi_fps(aviFile, fps);
			save_recordings();
		}
	}

    // Get file sizes
    fseek(aviFile, 0, SEEK_END);
    fseek(idxFile, 0, SEEK_END);
    long aviSize = ftell(aviFile);
    long idxSize = ftell(idxFile);
    long totalSize = aviSize + idxSize;

    // Reset file positions
    fseek(aviFile, 0, SEEK_SET);
    fseek(idxFile, 0, SEEK_SET);
	LOG_TRACE("%s: Uploading %s %ld\n",__func__,filename,totalSize);
    // Send response headers
    ACAP_HTTP_Respond_String(response, "status: 200 OK\r\n");
    ACAP_HTTP_Respond_String(response, "Content-Type: video/x-msvideo\r\n");
    ACAP_HTTP_Respond_String(response, "Content-Disposition: attachment; filename=%s\r\n", filename);
    ACAP_HTTP_Respond_String(response, "Content-Length: %ld\r\n", totalSize);
    ACAP_HTTP_Respond_String(response, "\r\n");

    // Use larger buffer for efficient transfer
    char* buffer = malloc(65536);
    if (!buffer) {
        fclose(aviFile);
        fclose(idxFile);
        ACAP_HTTP_Respond_Error(response, 500, "Memory allocation failed");
        return;
    }

    // Send AVI file content
    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, 65536, aviFile)) > 0) {
        if (ACAP_HTTP_Respond_Data(response, bytesRead, buffer) != 1) {
            break;  // Handle transfer interruption
        }
    }

    // Send index file content
    while ((bytesRead = fread(buffer, 1, 65536, idxFile)) > 0) {
        if (ACAP_HTTP_Respond_Data(response, bytesRead, buffer) != 1) {
            break;  // Handle transfer interruption
        }
    }

    free(buffer);
    fclose(aviFile);
    fclose(idxFile);
}

static void 
HTTP_Endpoint_Recordings(const ACAP_HTTP_Response response, 
                                     const ACAP_HTTP_Request request) {
    const char* method = ACAP_HTTP_Get_Method(request);

	LOG_TRACE("%s: %s\n",__func__,method);
    
    if (strcmp(method, "GET") == 0) {
        if (!Recordings_Container) {
            load_recordings();
        }

        const char* profileId = ACAP_HTTP_Request_Param(request, "id");
        if (profileId) {
            cJSON* recording = cJSON_GetObjectItem(Recordings_Container, profileId);
            if (!recording) {
                ACAP_HTTP_Respond_Error(response, 404, "Recording not found");
                return;
            }
            ACAP_HTTP_Respond_JSON(response, recording);
        } else {
            ACAP_HTTP_Respond_JSON(response, Recordings_Container);
        }
        return;
    }

	if (strcmp(method, "DELETE") == 0) {
		const char* profileId = ACAP_HTTP_Request_Param(request, "id");
		if (!profileId) {
			ACAP_HTTP_Respond_Error(response, 400, "Missing profile ID");
			return;
		}

		char path[PATH_MAX_LEN];
		sprintf(path, "/var/spool/storage/SD_DISK/timelapse2/%s", profileId);
		
		// Remove all files in directory
		DIR* dir = opendir(path);
		if (dir) {
			struct dirent* entry;
			while ((entry = readdir(dir))) {
				if (strcmp(entry->d_name, ".") == 0 || 
					strcmp(entry->d_name, "..") == 0)
					continue;
					
				char filepath[PATH_MAX_LEN];
				sprintf(filepath, "%s/%s", path, entry->d_name);
				unlink(filepath);
			}
			closedir(dir);
			
			// Remove the directory itself
			rmdir(path);
		}

		// Remove from recordings container
		if (Recordings_Container) {
			cJSON_DeleteItemFromObject(Recordings_Container, profileId);
			save_recordings();
		}

		ACAP_HTTP_Respond_Text(response, "Recording deleted");
		return;
	}

    ACAP_HTTP_Respond_Error(response, 405, "Method not allowed");
}


int
Recordings_Init(void) {
    LOG_TRACE("%s:\n", __func__);
    Recordings_Container = load_recordings();
    ACAP_HTTP_Node("recordings", HTTP_Endpoint_Recordings);
    ACAP_HTTP_Node("image", HTTP_Endpoint_Image);
    ACAP_HTTP_Node("export", HTTP_Endpoint_Export);
    return 0;
}
