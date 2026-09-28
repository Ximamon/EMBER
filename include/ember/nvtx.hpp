/**
 * @file nvtx.hpp
 * @author Julian Hinojosa (@jhg45-ua)
 * @brief NVTX profiling instrumentation wrapper and RAII scoped ranges.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

#include <cstdint>

#ifndef EMBER_ENABLE_NVTX
#define EMBER_ENABLE_NVTX 0
#endif

#if EMBER_ENABLE_NVTX
#include <nvtx3/nvToolsExt.h>
#endif

namespace ember::nvtx {

#if EMBER_ENABLE_NVTX

// ARGB Color Palette (0xAARRGGBB) for visual discrimination in NVIDIA Nsight Systems
constexpr std::uint32_t green  = 0xFF2ECC71U;
constexpr std::uint32_t blue   = 0xFF3498DBU;
constexpr std::uint32_t yellow = 0xFFF1C40FU;
constexpr std::uint32_t purple = 0xFF9B59B6U;
constexpr std::uint32_t red    = 0xFFE74C3CU;
constexpr std::uint32_t orange = 0xFFE67E22U;
constexpr std::uint32_t teal   = 0xFF1ABC9CU;

/**
 * @class ScopedRange
 * @brief RAII profiling range for NVIDIA Nsight Systems.
 * 
 * Automatically pushes an NVTX range upon instantiation and pops it upon destruction.
 */
class ScopedRange {
public:
    /**
     * @brief Constructs a scoped profiling range.
     * @param name ASCII label for the range in the Nsight timeline.
     * @param color ARGB color code for timeline rendering.
     * @param category Profiling category ID.
     */
    explicit ScopedRange(const char* name, std::uint32_t color = 0, std::uint32_t category = 0) noexcept {
        nvtxEventAttributes_t eventAttrib = {};
        eventAttrib.version = NVTX_VERSION;
        eventAttrib.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
        eventAttrib.messageType = NVTX_MESSAGE_TYPE_ASCII;
        eventAttrib.message.ascii = name;

        if (color != 0) {
            eventAttrib.colorType = NVTX_COLOR_ARGB;
            eventAttrib.color = color;
        }

        if (category != 0) {
            eventAttrib.category = category;
        }

        nvtxRangePushEx(&eventAttrib);
    }

    /**
     * @brief Destructor. Pops the current NVTX range.
     */
    ~ScopedRange() noexcept {
        nvtxRangePop();
    }

    ScopedRange(const ScopedRange&) = delete;
    ScopedRange& operator=(const ScopedRange&) = delete;
    ScopedRange(ScopedRange&&) = delete;
    ScopedRange& operator=(ScopedRange&&) = delete;
};

#else // EMBER_ENABLE_NVTX == 0

// No-op constants for clean compilation without NVTX
constexpr std::uint32_t green  = 0;
constexpr std::uint32_t blue   = 0;
constexpr std::uint32_t yellow = 0;
constexpr std::uint32_t purple = 0;
constexpr std::uint32_t red    = 0;
constexpr std::uint32_t orange = 0;
constexpr std::uint32_t teal   = 0;

/**
 * @class ScopedRange
 * @brief No-op implementation of ScopedRange compiling to zero instructions when NVTX is disabled.
 */
class ScopedRange {
public:
    explicit ScopedRange(const char* /*name*/, std::uint32_t /*color*/ = 0, std::uint32_t /*category*/ = 0) noexcept {}
    ~ScopedRange() = default;

    ScopedRange(const ScopedRange&) = delete;
    ScopedRange& operator=(const ScopedRange&) = delete;
    ScopedRange(ScopedRange&&) = delete;
    ScopedRange& operator=(ScopedRange&&) = delete;
};

#endif

} // namespace ember::nvtx