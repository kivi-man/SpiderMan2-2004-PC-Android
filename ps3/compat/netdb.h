#pragma once
// PSL1GHT's netdb with getaddrinfo and freeaddrinfo added (IPv4 through gethostbyname, ps3/net.cpp)
#include <net/netdb.h>
#include <netinet/in.h>
#ifdef __cplusplus
extern "C" {
#endif
int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res);
void freeaddrinfo(struct addrinfo* ai);
#ifdef __cplusplus
}
#endif
