//
//  media-reader.cpp
//  librfm-example
//
//  Created by Tambet Ingo on 06/11/15.
//  Copyright © 2015 litl. All rights reserved.
//

#include <string>
#include <cstdlib>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <libexif/exif-data.h>

#include "media-reader.hpp"
#include <log.h>

static void trim_spaces(char *buf) {
    char *s = buf-1;
    for (; *buf; ++buf) {
        if (*buf != ' ')
            s = buf;
    }
    *++s = 0; /* nul terminate the string on the first of the final spaces */
}

static int ReadExifInt(ExifData* ed, ExifTag tag) {
    ExifEntry *entry = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF], tag);
    if (entry) {
        char resbuf[1024];
        exif_entry_get_value(entry, resbuf, sizeof(resbuf));
        trim_spaces(resbuf);
        if (*resbuf) {
            return atoi(resbuf);
        }
    }
    
    return 0;
}

static time_t ReadExifDate(ExifData* ed) {
    ExifEntry *entry = exif_content_get_entry(ed->ifd[EXIF_IFD_0], EXIF_TAG_DATE_TIME);
    if (entry) {
        char resbuf[1024];
        exif_entry_get_value(entry, resbuf, sizeof(resbuf));
        trim_spaces(resbuf);
        if (*resbuf) {
            struct tm t;
            memset(&t, 0, sizeof(struct tm));
            strptime(resbuf, "%Y:%m:%d %H:%M:%S", &t);
            time_t ctime = timegm(&t);
            return ctime;
        }
    }
    
    return 0;
}

static void ReadSize(const std::string& path, ExifData* ed, rfm::Media_Size* size) {
    std::string url("file://");
    url.append(path);
    size->set_url(url);
    size->set_is_original(true);
    size->set_mime_type("image/jpeg");
    size->set_width(ReadExifInt(ed, EXIF_TAG_PIXEL_X_DIMENSION));
    size->set_height(ReadExifInt(ed, EXIF_TAG_PIXEL_Y_DIMENSION));
    
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) == 0) {
        size->set_file_size(file_stat.st_size);
    }
}

std::vector<rfm::Media> ReadMediaDirectory(const rfm::Source& source,
                                           const char* directory) {
    std::vector<rfm::Media> medias;

    DIR* dir = opendir(directory);
    if (!dir) {
        rfm::LOGE("Could not read directory %s", directory);
        return medias;
    }
    
    struct dirent* dp;
    while ((dp = readdir(dir)) != nullptr) {
        if (dp->d_type == DT_REG) {
            std::string filename(dp->d_name, dp->d_namlen);
            std::string path(directory);
            path.push_back('/');
            path.append(filename);
            
            ExifData* ed = exif_data_new_from_file(path.c_str());
            if (!ed) {
                rfm::LOGD("%s not an image", filename.c_str());
                continue;
            }
            
            medias.emplace_back();
            auto& media = medias.back();
            
            auto handle = media.mutable_handle();
            handle->set_source_type(source.type());
            handle->set_source_handle(source.handle());
            handle->set_handle(filename);
            
            media.set_filename(filename);
            media.set_original_path(path);
            media.set_title(filename);
            media.set_captured_at(ReadExifDate(ed));

            auto size = media.add_sizes();
            ReadSize(path, ed, size);
            exif_data_unref(ed);
        }
    }
    
    closedir(dir);
    return medias;
}

