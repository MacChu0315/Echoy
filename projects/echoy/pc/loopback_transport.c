/* GSP's local request/reply stream otherwise incurs TCP delayed-ACK stalls.
 * This Linux-only adapter changes an advisory option on our loopback sockets;
 * it never alters protocol bytes, SDK internals, remote sockets or system policy. */
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
ssize_t __real_recv(int fd, void *buffer, size_t length, int flags);
ssize_t __wrap_recv(int fd, void *buffer, size_t length, int flags)
{
    ssize_t result = __real_recv(fd, buffer, length, flags);
    int saved_errno = errno;
    if (result > 0) {
        struct sockaddr_storage peer;
        socklen_t size = sizeof(peer);
        if (getpeername(fd, (struct sockaddr *)&peer, &size) == 0 && peer.ss_family == AF_INET) {
            const struct sockaddr_in *address = (const struct sockaddr_in *)&peer;
            if ((ntohl(address->sin_addr.s_addr) & 0xff000000u) == 0x7f000000u) {
                const int enabled = 1;
                (void)setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &enabled, sizeof(enabled));
            }
        }
    }
    errno = saved_errno;
    return result;
}
