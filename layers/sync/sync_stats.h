/* Copyright (c) 2025 The Khronos Group Inc.
 * Copyright (c) 2025 Valve Corporation
 * Copyright (c) 2025 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>
#include <cstdint>

#ifndef VVL_ENABLE_SYNCVAL_STATS
#define VVL_ENABLE_SYNCVAL_STATS 0
#endif

#if VVL_ENABLE_SYNCVAL_STATS != 0
#include <atomic>

// NOTE: mimalloc should be built with MI_STAT=1 to enable stats module
#if defined(USE_MIMALLOC)
#include <mimalloc.h>
#if MI_MALLOC_VERSION >= 300
#define USE_MIMALLOC_STATS
#include "mimalloc-stats.h"
#include <mutex>
#endif
#endif  // defined(USE_MIMALLOC)

#endif  // VVL_ENABLE_SYNCVAL_STATS != 0

namespace syncval_stats {
#if VVL_ENABLE_SYNCVAL_STATS != 0

struct Value32 {
    std::atomic_uint32_t u32;
    void Update(uint32_t new_value);
    uint32_t Add(uint32_t n);  // Returns new counter value
    uint32_t Sub(uint32_t n);  // Returns new counter value
};

struct Value64 {
    std::atomic_uint64_t u64;
    void Update(uint64_t new_value);
    uint64_t Add(uint64_t n);  // Returns new counter value
    uint64_t Sub(uint64_t n);  // Returns new counter value
};

struct ValueMax32 {
    Value32 value;
    Value32 max_value;
    void Update(uint32_t new_value);
    void Add(uint32_t n);
    void Sub(uint32_t n);
};

struct ValueMax64 {
    Value64 value;
    Value64 max_value;
    void Update(uint64_t new_value);
    void Add(uint64_t n);
    void Sub(uint64_t n);
};

struct Stats {
    ~Stats();
    bool report_on_destruction = false;

#if defined(USE_MIMALLOC_STATS)
    mi_stats_t mi_stats;
    std::mutex mi_stats_mutex;
#endif

    ValueMax32 command_buffer_context_counter;
    void AddCommandBufferContext();
    void RemoveCommandBufferContext();

    ValueMax32 queue_batch_context_counter;
    void AddQueueBatchContext();
    void RemoveQueueBatchContext();

    ValueMax32 timeline_signal_counter;
    void AddTimelineSignals(uint32_t count);
    void RemoveTimelineSignals(uint32_t count);

    ValueMax32 unresolved_batch_counter;
    void AddUnresolvedBatch();
    void RemoveUnresolvedBatch();

    ValueMax32 handle_record_counter;
    void AddHandleRecord(uint32_t count = 1);
    void RemoveHandleRecord(uint32_t count = 1);

    void UpdateMemoryStats();
    void ReportOnDestruction();
    std::string CreateReport();
};

#else
struct Stats {
    void AddHandleRecord(uint32_t count = 1) {}
    void RemoveHandleRecord(uint32_t count = 1) {}
    void AddCommandBufferContext() {}
    void RemoveCommandBufferContext() {}
    void AddQueueBatchContext() {}
    void RemoveQueueBatchContext() {}
    void AddTimelineSignals(uint32_t count) {}
    void RemoveTimelineSignals(uint32_t count) {}
    void AddUnresolvedBatch() {}
    void RemoveUnresolvedBatch() {}

    void UpdateMemoryStats() {}
    void ReportOnDestruction() {}
    std::string CreateReport() { return "SyncVal stats are disabled in the current build configuration\n"; }
};
#endif  // VVL_ENABLE_SYNCVAL_STATS != 0
}  // namespace syncval_stats
