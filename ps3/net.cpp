// getaddrinfo for PSL1GHT, which has only gethostbyname: IPv4, one address
#include <netdb.h>

#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>

extern "C" int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res) {
    *res = nullptr;
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(service ? std::atoi(service) : 0));
    if (!node) {
        a.sin_addr.s_addr = hints && (hints->ai_flags & AI_PASSIVE) ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
    } else if (inet_aton(node, &a.sin_addr) == 0) {
        hostent* h = gethostbyname(node);
        if (!h || h->h_addrtype != AF_INET || !h->h_addr_list || !h->h_addr_list[0]) return -2;  // EAI_NONAME
        std::memcpy(&a.sin_addr, h->h_addr_list[0], 4);
    }
    auto* ai = static_cast<addrinfo*>(std::calloc(1, sizeof(addrinfo) + sizeof(sockaddr_in)));
    if (!ai) return -10;  // EAI_MEMORY
    ai->ai_family = AF_INET;
    ai->ai_socktype = hints ? hints->ai_socktype : 0;
    ai->ai_protocol = hints ? hints->ai_protocol : 0;
    ai->ai_addrlen = sizeof(sockaddr_in);
    ai->ai_addr = reinterpret_cast<sockaddr*>(ai + 1);
    std::memcpy(ai->ai_addr, &a, sizeof a);
    *res = ai;
    return 0;
}

extern "C" void freeaddrinfo(struct addrinfo* ai) {
    while (ai) {
        addrinfo* next = ai->ai_next;
        std::free(ai);
        ai = next;
    }
}
