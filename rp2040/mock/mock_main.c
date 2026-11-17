// cpu8 loader mock: runs the firmware's command parser on a PC, behind a
// pseudo-terminal that cpu8load can open like /dev/ttyACM0.
//
//   ./cpu8mock [-v] [-l LINK]
// prints "pty: /dev/pts/N"; with -l it also creates the symlink LINK -> pty.

#define _XOPEN_SOURCE 600
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "hw.h"
#include "proto.h"

extern int mock_fd, mock_verbose;
void mock_cpu_update(void);

static const char *link_path;

static void on_signal(int sig)
{
    (void)sig;
    if (link_path)
        unlink(link_path);
    _exit(0);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v"))
            mock_verbose = 1;
        else if (!strcmp(argv[i], "-l") && i + 1 < argc)
            link_path = argv[++i];
        else {
            fprintf(stderr, "usage: %s [-v] [-l LINK]\n", argv[0]);
            return 2;
        }
    }

    mock_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (mock_fd < 0 || grantpt(mock_fd) || unlockpt(mock_fd)) {
        perror("posix_openpt");
        return 1;
    }
    const char *slave = ptsname(mock_fd);
    // Keep the slave side open ourselves, so the master does not see a
    // hangup every time cpu8load exits; and make it raw (no echo).
    int keep = open(slave, O_RDWR | O_NOCTTY);
    struct termios t;
    if (keep < 0 || tcgetattr(keep, &t)) {
        perror(slave);
        return 1;
    }
    t.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t.c_oflag &= ~(tcflag_t)OPOST;
    t.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tcsetattr(keep, TCSANOW, &t);
    fcntl(mock_fd, F_SETFL, O_NONBLOCK);

    if (link_path) {
        unlink(link_path);
        if (symlink(slave, link_path)) {
            perror(link_path);
            return 1;
        }
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    printf("pty: %s\n", slave);
    fflush(stdout);

    for (;;) {
        struct pollfd p = { mock_fd, POLLIN, 0 };
        if (poll(&p, 1, 1) > 0 && (p.revents & POLLIN)) {
            char buf[256];
            ssize_t n = read(mock_fd, buf, sizeof buf);
            for (ssize_t i = 0; i < n; i++) {
                if (mock_verbose)
                    fputc(buf[i], stderr);
                proto_input(buf[i]);
            }
        }
        mock_cpu_update();
        proto_poll();
    }
}
