#pragma once

#include <cstdint>

struct KLineData {
    uint32_t sym_id;
    int32_t time;
    uint32_t volume;
    uint32_t num_trades;
    int32_t open;
    int32_t close;
    int32_t high;
    int32_t low;
};
