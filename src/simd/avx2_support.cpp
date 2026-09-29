/**
 * @file avx2_support.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Implementation of runtime CPUID capability checks for AVX2 instruction sets.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/simd/avx2_support.hpp"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace ember {

/**
 * @brief Queries hardware and OS support for AVX2 instructions.
 * 
 * On MSVC, uses `__cpuid` and `__cpuidex` intrinsics along with `_xgetbv` to ensure
 * both the CPU microarchitecture and the OS kernel (via XSAVE) support AVX2 registers.
 * On GCC and Clang, relies on the `__builtin_cpu_supports("avx2")` built-in.
 * 
 * The result is cached in a static local boolean lambda executed once.
 * 
 * @return true if AVX2 instructions can be safely executed without illegal instruction faults.
 */
bool avx2_supported() noexcept {
    static const bool supported = []() noexcept {
#if defined(_MSC_VER)
        int registers[4]{};
        __cpuid(registers, 0);
        const auto maximum_leaf = static_cast<unsigned int>(registers[0]);
        if (maximum_leaf < 7U) {
            return false;
        }

        // Query leaf 1: check for OSXSAVE (ECX bit 27) and AVX (ECX bit 28)
        __cpuidex(registers, 1, 0);
        const bool os_supports_xsave = (registers[2] & (1 << 27)) != 0;
        const bool cpu_supports_avx = (registers[2] & (1 << 28)) != 0;
        if (!os_supports_xsave || !cpu_supports_avx) {
            return false;
        }
        // Verify XCR0 register: bits 1 (XMM) and 2 (YMM) must be enabled by the OS
        if ((_xgetbv(0) & 0x6U) != 0x6U) {
            return false;
        }

        // Query leaf 7, sub-leaf 0: check for AVX2 (EBX bit 5)
        __cpuidex(registers, 7, 0);
        return (registers[1] & (1 << 5)) != 0;
#elif defined(__GNUC__) || defined(__clang__)
        return __builtin_cpu_supports("avx2") != 0;
#else
        return false;
#endif
    }();
    return supported;
}

} // namespace ember
