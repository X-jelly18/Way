#include "scanner/netprobe.hpp"

#include <chrono>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace scanner {

bool network_up() {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo("m.google.com", nullptr, &hints, &res);
    if (rc != 0) return false;
    freeaddrinfo(res);
    return true;
}

int seed_initial_concurrency() {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("m.google.com", "443", &hints, &res) != 0 || !res)
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

}  // namespace scanner
