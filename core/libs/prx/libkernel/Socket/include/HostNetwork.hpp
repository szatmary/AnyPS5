#pragma once
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace HostNetwork {

inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("APS5_NETWORK");
        if (value == nullptr || std::string_view(value) == "0") return false;
        if (std::string_view(value) == "1") return true;
        throw std::runtime_error(std::string("APS5_NETWORK must be 0 or 1, not ") + value);
    }();
    return enabled;
}

inline bool Loopback(const sockaddr* address) {
    if (address->sa_family == AF_INET) return (ntohl(reinterpret_cast<const sockaddr_in*>(address)->sin_addr.s_addr) >> 24) == 127;
    if (address->sa_family != AF_INET6) return false;
    const auto& ip = reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr;
    return IN6_IS_ADDR_LOOPBACK(&ip) || (IN6_IS_ADDR_V4MAPPED(&ip) && ip.s6_addr[12] == 127);
}

inline bool Unspecified(const sockaddr* address) {
    if (address->sa_family == AF_INET) return reinterpret_cast<const sockaddr_in*>(address)->sin_addr.s_addr == htonl(INADDR_ANY);
    return address->sa_family == AF_INET6 && IN6_IS_ADDR_UNSPECIFIED(&reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr);
}

inline bool Reaches(const sockaddr* address) {
    return Enabled() || (address->sa_family != AF_INET && address->sa_family != AF_INET6) || Loopback(address) || Unspecified(address);
}

inline void SetError(const int error) {
#ifdef _WIN32
    WSASetLastError(error == ENETUNREACH ? WSAENETUNREACH : WSAEADDRNOTAVAIL);
#else
    errno = error;
#endif
}

template <typename Socket, typename Length>
int Connect(const Socket socket, const sockaddr* address, const Length length) {
    if (!Reaches(address)) {
        SetError(ENETUNREACH);
        return -1;
    }
    return ::connect(socket, address, length);
}

template <typename Socket, typename Buffer, typename Size, typename Length>
auto SendTo(const Socket socket, const Buffer buffer, const Size size, const int flags, const sockaddr* address, const Length length) -> decltype(::sendto(socket, buffer, size, flags, address, length)) {
    if (address != nullptr && !Reaches(address)) {
        SetError(ENETUNREACH);
        return -1;
    }
    return ::sendto(socket, buffer, size, flags, address, length);
}

template <typename Socket, typename Length>
int Bind(const Socket socket, const sockaddr* address, const Length length) {
    if (Enabled() || Loopback(address)) return ::bind(socket, address, length);
    sockaddr_storage local{};
    if (address->sa_family == AF_INET && static_cast<std::size_t>(length) >= sizeof(sockaddr_in)) {
        std::memcpy(&local, address, sizeof(sockaddr_in));
        auto& ip = reinterpret_cast<sockaddr_in&>(local).sin_addr;
        if (!Unspecified(address)) {
            SetError(EADDRNOTAVAIL);
            return -1;
        }
        ip.s_addr = htonl(INADDR_LOOPBACK);
        return ::bind(socket, reinterpret_cast<const sockaddr*>(&local), length);
    }
    if (address->sa_family == AF_INET6 && static_cast<std::size_t>(length) >= sizeof(sockaddr_in6)) {
        std::memcpy(&local, address, sizeof(sockaddr_in6));
        auto& ip = reinterpret_cast<sockaddr_in6&>(local).sin6_addr;
        if (!Unspecified(address)) {
            SetError(EADDRNOTAVAIL);
            return -1;
        }
        ip = in6addr_loopback;
        return ::bind(socket, reinterpret_cast<const sockaddr*>(&local), length);
    }
    return ::bind(socket, address, length);
}

inline bool Local(const char* name) {
    return std::string_view(name) == "localhost";
}

inline int GetAddrInfo(const char* node, const char* service, const addrinfo* hints, addrinfo** result) {
    if (Enabled() || node == nullptr || Local(node) || (hints != nullptr && (hints->ai_flags & AI_NUMERICHOST))) return ::getaddrinfo(node, service, hints, result);
    addrinfo numeric = hints != nullptr ? *hints : addrinfo{};
    numeric.ai_flags |= AI_NUMERICHOST;
    const int error = ::getaddrinfo(node, service, &numeric, result);
    return error == EAI_NONAME ? EAI_AGAIN : error;
}

inline bool ResolvesByName(const char* name) {
    in_addr numeric{};
    return Enabled() || Local(name) || inet_pton(AF_INET, name, &numeric) == 1;
}

inline hostent* GetHostByName(const char* name) {
    if (ResolvesByName(name)) return ::gethostbyname(name);
#ifdef _WIN32
    WSASetLastError(WSATRY_AGAIN);
#else
    h_errno = TRY_AGAIN;
#endif
    return nullptr;
}

#ifdef __linux__
inline int GetHostByNameR(const char* name, hostent* entry, char* buffer, const std::size_t size, hostent** result, int* hostError) {
    if (ResolvesByName(name)) return ::gethostbyname_r(name, entry, buffer, size, result, hostError);
    *result = nullptr;
    *hostError = TRY_AGAIN;
    return EAGAIN;
}
#endif

template <typename Length, typename HostLength, typename ServiceLength>
int GetNameInfo(const sockaddr* address, const Length length, char* host, const HostLength hostLength, char* service, const ServiceLength serviceLength, const int flags) {
    if (Enabled() || host == nullptr || (flags & NI_NUMERICHOST) || Loopback(address)) return ::getnameinfo(address, length, host, hostLength, service, serviceLength, flags);
    if (flags & NI_NAMEREQD) return EAI_AGAIN;
    return ::getnameinfo(address, length, host, hostLength, service, serviceLength, flags | NI_NUMERICHOST);
}

}
