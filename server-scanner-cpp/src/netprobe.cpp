#include "scanner/netprobe.hpp"

#include <chrono>
#include <cstring>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace scanner {

bool network_up(const std::string& host) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0) return false;
    freeaddrinfo(res);
    return true;
}

int seed_initial_concurrency(const std::string& host) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), "443", &hints, &res) != 0 || !res)
        return 15;

    auto start = std::chrono::steady_clock::now();
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bool ok = false;
    if (fd >= 0) {
        struct timeval tv{3, 0};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        ok = (::connect(fd, res->ai_addr, res->ai_addrlen) == 0);
        ::close(fd);
    }
    freeaddrinfo(res);
    if (!ok) return 15;

    double rtt = std::chrono::duration<double>(
                     std::chrono::steady_clock::now() - start).count();
    if (rtt < 0.15) return 80;
    if (rtt < 0.40) return 40;
    return 15;
}

std::optional<std::string> resolve_ip(const std::string& host, int family) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = family == 4 ? AF_INET : family == 6 ? AF_INET6 : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res)
        return std::nullopt;

    char buf[INET6_ADDRSTRLEN] = {0};
    const char* out = nullptr;
    if (res->ai_family == AF_INET) {
        auto* sa = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
        out = inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
    } else if (res->ai_family == AF_INET6) {
        auto* sa = reinterpret_cast<struct sockaddr_in6*>(res->ai_addr);
        out = inet_ntop(AF_INET6, &sa->sin6_addr, buf, sizeof(buf));
    }
    freeaddrinfo(res);
    if (!out) return std::nullopt;
    return std::string(buf);
}

}  // namespace scanner
