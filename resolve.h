#pragma once
#include <cstd/std.h>
#include <cstd/unix.h>
#include <fixedstr/fixedstr.h>

struct net_resolve {
	struct addrinfo *head;
	struct addrinfo *it;
};

/* Instance */
bool net_resolve_init(struct net_resolve *inst, const struct fstr *addr, const struct fstr *port, int family, int socktype, int protocol, int flags);
void net_resolve_destroy(struct net_resolve *inst);

/* Iterate */
const struct addrinfo *net_resolve_current(struct net_resolve *inst);
const struct addrinfo *net_resolve_next(struct net_resolve *inst);
void net_resolve_rewind(struct net_resolve *inst);

/* Use */
int net_resolve_bind(struct net_resolve *inst, int sockflags);
int net_resolve_connect(struct net_resolve *inst, int sockflags);

/* Shorthands */
int net_bind(const struct fstr *addr, const struct fstr *port, int family, int socktype, int protocol, int flags, int sockflags);
int net_connect(const struct fstr *addr, const struct fstr *port, int family, int socktype, int protocol, int flags, int sockflags);

/* Reverse */
bool net_resolve_reverse(const void *addr, size_t addrlen, struct fstr *host, struct fstr *port, int flags);
#define NRRF_NUMERIC (NI_NUMERICHOST | NI_NUMERICSERV)
