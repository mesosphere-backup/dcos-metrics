#pragma once

#include <glog/logging.h>

namespace metrics {
  /**
   * Sets the "F_CLOEXEC" bit on the provided UDP or TCP socket. When the mesos agent forks to
   * launch an executor, this bit ensures that the socket is not inherited by child processes.
   */
  template <typename Socket, typename Host>
  void set_cloexec(Socket& socket, const Host& host, size_t port) {
    int socket_flags = fcntl(socket.native_handle(), F_GETFD, 0);
    if (socket_flags < 0) {
      int errnum = errno;
      // TODO mark this ERROR (only FATAL for testing):
      LOG(FATAL) << "Unable to read socket flags for "
                 << host << ":" << port << ": errno=" << errnum << " => " << strerror(errnum);
    } else if (fcntl(socket.native_handle(), F_SETFD, socket_flags | FD_CLOEXEC) < 0) {
      int errnum = errno;
      // TODO mark this ERROR (only FATAL for testing):
      LOG(FATAL) << "Failed to set CLOEXEC on socket file descriptor for "
                 << host << ":" << port << ": errno=" << errnum << " => " << strerror(errnum);
    }
  }
}
