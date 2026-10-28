#include "prx/libkernel/include/Api.hpp"
#include "prx/libc/include/Api.hpp"
#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libSceNetCtl/include/NetCtl.hpp"
#include "prx/libScePosixForWebKit/include/Api.hpp"
#include "prx/libScePosixForWebKit/GuestResolver.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "SceTypes.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr int GuestInet = 2;
constexpr int GuestStream = 1;
constexpr int GuestDatagram = 2;
constexpr int GuestNetworkUnreachable = 51;
constexpr int GuestAddressNotAvailable = 49;
constexpr int GuestLookupAgain = 2;
constexpr std::uint32_t GuestLoopbackFlag = 0x8;
constexpr int NetCtlDisconnected = 0;
constexpr int NetCtlInfoIpAddress = 14;
constexpr int NetCtlNotConnected = static_cast<int>(0x80412108);

void Require(const bool condition, const char* what) {
    if (condition) return;
    std::fprintf(stderr, "network offline check failed: %s\n", what);
    std::abort();
}

std::array<std::uint8_t, 16> Ipv4(const std::uint8_t a, const std::uint8_t b, const std::uint8_t c, const std::uint8_t d, const std::uint16_t port) {
    return {16, GuestInet, static_cast<std::uint8_t>(port >> 8), static_cast<std::uint8_t>(port), a, b, c, d};
}

void ConsoleHasNoConnection() {
    Require(sceNetCtlInit() == 0, "sceNetCtlInit");
    int state = -1;
    Require(sceNetCtlGetState(&state) == 0 && state == NetCtlDisconnected, "the state is not disconnected");
    NetCtlInfo info{};
    Require(sceNetCtlGetInfo(NetCtlInfoIpAddress, &info) == NetCtlNotConnected, "an IP address is reported");
}

void OnlyLoopbackInterfacesAreListed() {
    GuestInterfaceAddress* list = nullptr;
    Require(getifaddrs_nid_postfix(&list) == 0 && list != nullptr, "getifaddrs lists nothing");
    for (auto* entry = list; entry; entry = entry->next) Require((entry->flags & GuestLoopbackFlag) != 0, "an interface other than loopback is listed");
    freeifaddrs_nid_postfix(list);
}

void OtherAddressesAreUnreachable() {
    const auto documentation = Ipv4(192, 0, 2, 1, 80);
    const int stream = socket_nid_postfix(GuestInet, GuestStream, 0);
    Require(stream >= 0, "stream socket");
    Require(connect_nid_postfix(stream, documentation.data(), documentation.size()) == -1 && *__error_nid_postfix() == GuestNetworkUnreachable,
        "a connection to another address did not fail with ENETUNREACH");
    Require(close_nid_postfix(stream) == 0, "close");
    const int datagram = socket_nid_postfix(GuestInet, GuestDatagram, 0);
    Require(datagram >= 0, "datagram socket");
    const char byte = 0;
    Require(sendto_nid_postfix(datagram, &byte, 1, 0, documentation.data(), documentation.size()) == -1 && *__error_nid_postfix() == GuestNetworkUnreachable,
        "a datagram to another address did not fail with ENETUNREACH");
    Require(close_nid_postfix(datagram) == 0, "close");
}

void WildcardBindsToLoopback() {
    const int listener = socket_nid_postfix(GuestInet, GuestStream, 0);
    Require(listener >= 0, "listener socket");
    auto any = Ipv4(0, 0, 0, 0, 0);
    Require(bind_nid_postfix(listener, any.data(), any.size()) == 0 && listen_nid_postfix(listener, 1) == 0, "a wildcard bind failed");
    std::uint32_t size = any.size();
    Require(getsockname_nid_postfix(listener, any.data(), &size) == 0 && any[4] == 127 && any[5] == 0 && any[6] == 0 && any[7] == 1,
        "a wildcard bind is not bound to loopback");
    const int client = socket_nid_postfix(GuestInet, GuestStream, 0);
    Require(client >= 0 && connect_nid_postfix(client, any.data(), any.size()) == 0, "loopback is unreachable");
    const int accepted = accept_nid_postfix(listener, nullptr, nullptr);
    Require(accepted >= 0, "accept");
    for (const int descriptor : {client, accepted, listener}) Require(close_nid_postfix(descriptor) == 0, "close");

    const int other = socket_nid_postfix(GuestInet, GuestStream, 0);
    const auto documentation = Ipv4(192, 0, 2, 1, 0);
    Require(other >= 0 && bind_nid_postfix(other, documentation.data(), documentation.size()) == -1 && *__error_nid_postfix() == GuestAddressNotAvailable,
        "a bind to another address did not fail with EADDRNOTAVAIL");
    Require(close_nid_postfix(other) == 0, "close");
}

void OnlyLocalNamesResolve() {
    GuestResolver::AddressInfo hints{};
    hints.family = GuestInet;
    hints.socketType = GuestStream;
    GuestResolver::AddressInfo* result = nullptr;
    Require(getaddrinfo_nid_postfix("example.com", "80", &hints, &result) == GuestLookupAgain && result == nullptr, "a name resolved without networking");
    for (const char* local : {"localhost", "192.0.2.1"}) {
        Require(getaddrinfo_nid_postfix(local, "80", &hints, &result) == 0 && result != nullptr, "localhost or a numeric host did not resolve");
        freeaddrinfo_nid_postfix(result);
        result = nullptr;
    }
}

}

int main() {
#ifdef _WIN32
    Require(_putenv_s("APS5_NETWORK", "0") == 0, "APS5_NETWORK");
#else
    Require(setenv("APS5_NETWORK", "0", 1) == 0, "APS5_NETWORK");
#endif
    std::array<void*, 10> heap{};
    ApplicationHeapRegister_nid_no_patch(heap.data());
    ConsoleHasNoConnection();
    OnlyLoopbackInterfacesAreListed();
    OtherAddressesAreUnreachable();
    WildcardBindsToLoopback();
    OnlyLocalNamesResolve();
    std::puts("network offline checks passed");
    return 0;
}
