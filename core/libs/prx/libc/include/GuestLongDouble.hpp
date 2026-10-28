#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_GUESTLONGDOUBLE_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_GUESTLONGDOUBLE_HPP

#include <array>
#include <cerrno>
#include <cfenv>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

static_assert(std::numeric_limits<long double>::digits >= 64 && std::numeric_limits<long double>::max_exponent == 16384, "host long double must hold every x87 extended value");

#if LDBL_MANT_DIG != 64
#ifndef ANYPS5_GUEST_LIFTED
#error "a host long double other than x87 extended needs the lifted guest runtime"
#endif
extern "C" {
void GuestCpuLoadExtended_nid_no_patch(const void* guest, long double* host);
void GuestCpuStoreExtended_nid_no_patch(const long double* host, void* guest);
}
#endif

namespace GuestLongDouble {

inline long double Load(const void* guest) {
    long double value;
#if LDBL_MANT_DIG == 64
    std::memset(&value, 0, sizeof(value));
    std::memcpy(&value, guest, 10);
#else
    GuestCpuLoadExtended_nid_no_patch(guest, &value);
#endif
    return value;
}

inline void Store(void* guest, const long double value) {
#if LDBL_MANT_DIG == 64
    std::memcpy(guest, &value, 10);
#else
    GuestCpuStoreExtended_nid_no_patch(&value, guest);
#endif
}

inline std::array<unsigned char, 16> Bytes(const long double value) {
    std::array<unsigned char, 16> bytes{};
    Store(bytes.data(), value);
    return bytes;
}

inline long double Round(const long double value) {
    return Load(Bytes(value).data());
}

inline long double Parse(const char* text, char** end) {
#if LDBL_MANT_DIG == 64
    return std::strtold(text, end);
#else
    const int rounding = std::fegetround();
    const int saved = errno;
    std::fesetround(FE_DOWNWARD);
    const long double below = std::strtold(text, end);
    std::fesetround(FE_UPWARD);
    errno = saved;
    const long double above = std::strtold(text, nullptr);
    std::fesetround(rounding);
    long double truncated = std::fabs(below) <= std::fabs(above) ? below : above;
    if (below != above && !std::isnan(below)) {
        unsigned char bits[sizeof(long double)];
        std::memcpy(bits, &truncated, sizeof(truncated));
        bits[0] |= 1;
        std::memcpy(&truncated, bits, sizeof(truncated));
    }
    const long double value = Round(truncated);
    if (std::isinf(value) && !std::isinf(truncated)) errno = ERANGE;
    return value;
#endif
}

}

#endif
