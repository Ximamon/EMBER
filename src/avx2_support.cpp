#include "avx2_support.hpp"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace ember {

bool avx2_supported() noexcept {
    static const bool supported = []() noexcept {
#if defined(_MSC_VER)
        int registers[4]{};
        __cpuid(registers, 0);
        const auto maximum_leaf = static_cast<unsigned int>(registers[0]);
        if (maximum_leaf < 7U) {
            return false;
        }

        __cpuidex(registers, 1, 0);
        const bool os_supports_xsave = (registers[2] & (1 << 27)) != 0;
        const bool cpu_supports_avx = (registers[2] & (1 << 28)) != 0;
        if (!os_supports_xsave || !cpu_supports_avx) {
            return false;
        }
        if ((_xgetbv(0) & 0x6U) != 0x6U) {
            return false;
        }

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
