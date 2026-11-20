// tcpsvd-lite PORT PROGRAM [ARGS...]: accept TCP connections and run PROGRAM for each one with the
// socket as stdin/stdout (inetd style). busybox on the P3 has no httpd/tcpsvd/inetd, and an
// `nc -l` loop serves one connection at a time, which browsers' spare connections block.
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 3) return 2;
  signal(SIGCHLD, SIG_IGN);  // no zombies
  int s = socket(AF_INET6, SOCK_STREAM, 0), one = 1, zero = 0;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);
  struct sockaddr_in6 a = {.sin6_family = AF_INET6, .sin6_port = htons(atoi(argv[1]))};
  if (bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 16)) return 1;
  for (;;) {
    int c = accept(s, 0, 0);
    if (c < 0) continue;
    if (fork() == 0) {
      close(s);
      dup2(c, 0);
      dup2(c, 1);
      close(c);
      signal(SIGCHLD, SIG_DFL);
      execv(argv[2], argv + 2);
      _exit(127);
    }
    close(c);
  }
}
