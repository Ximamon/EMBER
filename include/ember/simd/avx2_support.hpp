/**
 * @file avx2_support.hpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Dynamic runtime detection of AVX2 and FMA CPU vector extensions.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#pragma once

namespace ember {

/**
 * @brief Queries CPUID capabilities at runtime to check for AVX2 and OSXSAVE support.
 * @return true if the host CPU and operating system support executing AVX2 instructions, false otherwise.
 */
bool avx2_supported() noexcept;

} // namespace ember
