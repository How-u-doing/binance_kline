#pragma once

#include "yyjson.h"

#include <string_view>
#include <iostream>
#include <cstdio>
#include <ctime>
#include <cstring>

inline const char *timestamp_ms_to_str(time_t timestamp_ms) {
    // e.g., "UTC: 2024-12-09 05:27:00.000"
    thread_local char utc_buf[96] = {};  // 82 bytes at most
    time_t timestamp_sec = timestamp_ms / 1000;
    int milliseconds = timestamp_ms % 1000;

    // convert timestamp to UTC struct tm
    struct tm *utc_time = gmtime(&timestamp_sec);
    if (!utc_time) {
        perror("gmtime");
        return "";
    }

    sprintf(utc_buf, "UTC: %04d-%02d-%02d %02d:%02d:%02d.%03d", utc_time->tm_year + 1900,
            utc_time->tm_mon + 1, utc_time->tm_mday, utc_time->tm_hour, utc_time->tm_min,
            utc_time->tm_sec, milliseconds);
    return utc_buf;
}

inline uint64_t kline_get_open_time(yyjson_val *k_obj) {
    return yyjson_get_uint(yyjson_obj_get(k_obj, "t"));
}

inline uint64_t kline_get_close_time(yyjson_val *k_obj) {
    return yyjson_get_uint(yyjson_obj_get(k_obj, "T"));
}

inline const char *kline_get_open(yyjson_val *k_obj) {
    return yyjson_get_str(yyjson_obj_get(k_obj, "o"));
}

inline const char *kline_get_close(yyjson_val *k_obj) {
    return yyjson_get_str(yyjson_obj_get(k_obj, "c"));
}

inline const char *kline_get_high(yyjson_val *k_obj) {
    return yyjson_get_str(yyjson_obj_get(k_obj, "h"));
}

inline const char *kline_get_low(yyjson_val *k_obj) {
    return yyjson_get_str(yyjson_obj_get(k_obj, "l"));
}

inline const char *kline_get_volume(yyjson_val *k_obj) {
    return yyjson_get_str(yyjson_obj_get(k_obj, "v"));
}

inline bool kline_is_closed(yyjson_val *k_obj) {
    return yyjson_get_bool(yyjson_obj_get(k_obj, "x"));
}

inline void print_kline_data(std::string_view message) {
    yyjson_doc *doc = yyjson_read(message.data(), message.size(), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *k_obj = yyjson_obj_get(root, "k");
    if (k_obj) {
        uint64_t event_time = yyjson_get_uint(yyjson_obj_get(root, "E"));
        std::cout << "Event time: " << timestamp_ms_to_str(event_time) << "\n"
                  << "Symbol: " << yyjson_get_str(yyjson_obj_get(root, "s")) << "\n"
                  << "Kline data:\n"
                  << "  Start time: " << timestamp_ms_to_str(kline_get_open_time(k_obj)) << "\n"
                  << "  Open: " << kline_get_open(k_obj) << "\n"
                  << "  High: " << kline_get_high(k_obj) << "\n"
                  << "  Low: " << kline_get_low(k_obj) << "\n"
                  << "  Close: " << kline_get_close(k_obj) << "\n"
                  << "  Volume: " << kline_get_volume(k_obj) << "\n"
                  << "  Is kline closed: " << kline_is_closed(k_obj) << "\n\n";
    }
    yyjson_doc_free(doc);
}
