#if 0
(
set -eu
gcc -Ic_modules -DSIMPLE_LOGGING -DTEST_resolve -std=gnu99 -O2 -Wall -Wextra -Wno-parentheses -Werror -o resolve $(find -name '*.c')
if (( $# == 0 )); then
	set -- -qf n www.google.co.ck ftp
fi
# glibc 2.24-2 has intentional "leak" in getaddrinfo which shows up as
# "definitely lost" in valgrind, but the resource is re-used in subsequent
# calls - solution is to wait for a valgrind update I guess.
# valgrind --quiet --leak-check=full --track-origins=yes ./resolve "$@"
./resolve "$@"
)
exit 0
#endif
#include "resolve.h"

bool net_resolve_init(struct net_resolve *inst, const struct fstr *addr, const struct fstr *port, int family, int socktype, int protocol, int flags)
{
	inst->head = NULL;
	inst->it = NULL;
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = socktype;
	hints.ai_protocol = protocol;
	hints.ai_flags = flags;
	int err = getaddrinfo(fstr_ptr(addr), fstr_ptr(port), &hints, &inst->head);
	if (err != 0) {
		log_sysfail("getaddrinfo", PRIfs ", " PRIfs ", [%d, %d, %d, %d]", prifs(addr), prifs(port), family, socktype, protocol, flags);
		log_error("getaddrinfo failed: %s", gai_strerror(err));
		freeaddrinfo(inst->head);
		inst->head = NULL;
		return false;
	}
	net_resolve_rewind(inst);
	return true;
}

void net_resolve_destroy(struct net_resolve *inst)
{
	freeaddrinfo(inst->head);
}

const struct addrinfo *net_resolve_current(struct net_resolve *inst)
{
	return inst->it;
}

const struct addrinfo *net_resolve_next(struct net_resolve *inst)
{
	const struct addrinfo *it = inst->it;
	if (it == NULL) {
		return NULL;
	}
	inst->it = inst->it->ai_next;
	return it;
}

void net_resolve_rewind(struct net_resolve *inst)
{
	inst->it = inst->head;
}

int net_resolve_bind(struct net_resolve *inst, int sockflags)
{
#define SOCKETARGS_ ai->ai_family, ai->ai_socktype | sockflags, ai->ai_protocol
	net_resolve_rewind(inst);
	const struct addrinfo *ai;
	while ((ai = net_resolve_next(inst))) {
		int fd = socket(SOCKETARGS_);
		if (fd == -1) {
			log_sysfail("socket", "%d, %d, %d", SOCKETARGS_);
			return -1;
		}
		int err = bind(fd, ai->ai_addr, ai->ai_addrlen);
		if (err == 0) {
			return fd;
		}
		close(fd);
	}
#undef SOCKETARGS_
	return -1;
}

int net_resolve_connect(struct net_resolve *inst, int sockflags)
{
#define SOCKETARGS_ ai->ai_family, ai->ai_socktype | sockflags, ai->ai_protocol
	net_resolve_rewind(inst);
	const struct addrinfo *ai;
	while ((ai = net_resolve_next(inst))) {
		int fd = socket(SOCKETARGS_);
		if (fd == -1) {
			log_sysfail("socket", "%d, %d, %d", SOCKETARGS_);
			return -1;
		}
		int err = connect(fd, ai->ai_addr, ai->ai_addrlen);
		if (err == 0) {
			return fd;
		}
		close(fd);
	}
#undef SOCKETARGS_
	return -1;
}

int net_bind(const struct fstr *addr, const struct fstr *port, int family, int socktype, int protocol, int flags, int sockflags)
{
	struct net_resolve nr;
	if (!net_resolve_init(&nr, addr, port, family, socktype, protocol, flags)) {
		return -1;
	}
	int fd = net_resolve_bind(&nr, sockflags);
	net_resolve_destroy(&nr);
	return fd;
}

int net_connect(const struct fstr *addr, const struct fstr *port, int family, int socktype, int protocol, int flags, int sockflags)
{
	struct net_resolve nr;
	if (!net_resolve_init(&nr, addr, port, family, socktype, protocol, flags)) {
		return -1;
	}
	int fd = net_resolve_connect(&nr, sockflags);
	net_resolve_destroy(&nr);
	return fd;
}

bool net_resolve_reverse(const void *addr, size_t addrlen, struct fstr *host, struct fstr *port, int flags)
{
	fstr_alloc(host, NI_MAXHOST);
	fstr_alloc(port, NI_MAXSERV);
	int err = getnameinfo(addr, addrlen, fstr_get_mutable(host), NI_MAXHOST, fstr_get_mutable(port), NI_MAXSERV, flags);
	if (err) {
		log_sysfail("getnameinfo", "");
		log_error("getnameinfo failed: %s", gai_strerror(err));
	}
	return err == 0;
}

#if defined TEST_resolve
#include <debug/hexdump.h>

static void help_args(const char *name)
{
	printf("Syntax: %s [-qacs46] [-f {r|t|n}] [host [service]]\nSyntax: %s [-qacs46] [-f {r|t|n}] -p service [host]\n\n"
		"Where:\n"
		"\t-q\tShow result only\n"
		"\t-a\tShow all results\n"
		"\t-c\tShow canonical name\n"
		"\t-s\tResolve for use in server\n"
		"\t-4,-6\tUse IPv4/IPv6 only\n"
		"\thost\tName/number/address of host\n"
		"\tservice\tname/number of service\n"
		"\tformat\tformat of results [raw/text/numeric]\n"
		"\n", name, name);
}

int main(int argc, char *argv[])
{
	bool quiet = false;
	bool all = false;
	bool passive = false;
	bool canon = false;
	bool ip4 = false;
	bool ip6 = false;
	struct fstr port = FSTR_INIT;
	struct fstr host = FSTR_INIT;
	int c;
	const char *name = argv[0];
	const char *format = "n";
	while ((c = getopt(argc, argv, "qasc46p:f:")) != -1) {
		switch (c) {
		case '4': ip4 = true; break;
		case '6': ip6 = true; break;
		case 'q': quiet = true; break;
		case 'a': all = true; break;
		case 's': passive = true; break;
		case 'c': canon = true; break;
		case 'p': fstr_set_ref(&port, optarg); break;
		case 'f': format = optarg; break;
		case '?': help_args(name); return 1;
		}
	}
	int family = AF_UNSPEC;
	if (ip4 && ip6) {
		help_args(name);
		return 1;
	} else if (ip4) {
		family = AF_INET;
	} else if (ip6) {
		family = AF_INET6;
	}
	int fmti = -1;
	if (strcmp(format, "raw") == 0 || strcmp(format, "r") == 0) {
		fmti = 0;
	}
	if (strcmp(format, "text") == 0 || strcmp(format, "t") == 0) {
		fmti = 1;
	}
	if (strcmp(format, "numeric") == 0 || strcmp(format, "n") == 0) {
		fmti = 2;
	}
	if (fmti == -1) {
		help_args(name);
		return 1;
	}
	argc -= optind;
	argv += optind;
	if (argc) {
		fstr_set_ref(&host, argv[0]);
		argc--;
		argv++;
	}
	if (argc) {
		if (!fstr_empty(&port)) {
			help_args(name);
			return 1;
		}
		fstr_set_ref(&port, argv[0]);
		argc--;
		argv++;
	}
	if (argc) {
		help_args(name);
		return 1;
	}
	struct net_resolve nr;
	if (!net_resolve_init(&nr, &host, &port, family, SOCK_STREAM, 0, (passive ? AI_PASSIVE : 0) | (canon ? AI_CANONNAME : 0))) {
		fprintf(stderr, "Failed to resolve " PRIfs ":" PRIfs "\n", prifs(&host), prifs(&port));
		return 1;
	}
	const struct addrinfo *ai = net_resolve_current(&nr);
	if (canon) {
		if (quiet) {
			printf("%s\n", ai ? ai->ai_canonname : "");
		} else if (ai) {
			printf("Canonical name: %s\n\n", ai->ai_canonname);
		}
	}
	int i = 0;
	while ((ai = net_resolve_next(&nr))) {
		if (!quiet) {
			printf("Result #%d:\n", ++i);
			printf("  Family     : %d\n", ai->ai_family);
			printf("  Socket type: %d\n", ai->ai_socktype);
			printf("  Protocol   : %d\n", ai->ai_protocol);
			printf("  Address    : ");
		}
		struct fstr h = FSTR_INIT;
		struct fstr s = FSTR_INIT;
		bool res;
		if (fmti == 0) {
			printf("\n");
			hexcat(ai->ai_addr, ai->ai_addrlen, 0);
		} else if (fmti == 1) {
			res = net_resolve_reverse(ai->ai_addr, ai->ai_addrlen, &h, &s, 0);
		} else if (fmti == 2) {
			res = net_resolve_reverse(ai->ai_addr, ai->ai_addrlen, &h, &s, NRRF_NUMERIC);
		}
		if (fmti > 0) {
			if (res) {
				if (!quiet || !fstr_empty(&host) && !fstr_empty(&port)) {
					printf(PRIfs ":" PRIfs "\n", prifs(&h), prifs(&s));
				} else if (!fstr_empty(&host)) {
					printf(PRIfs "\n", prifs(&h));
				} else if (!fstr_empty(&port)) {
					printf(PRIfs "\n", prifs(&s));
				}
			}
		}
		fstr_destroy(&h);
		fstr_destroy(&s);
		if (!quiet) {
			printf("\n");
		}
		if (!all) {
			break;
		}
	}
	net_resolve_destroy(&nr);
	return 0;
}
#endif
