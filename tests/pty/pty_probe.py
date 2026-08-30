#!/usr/bin/env python3
"""pty_probe.py - run a guest shell under a real PTY and capture output.

Usage: pty_probe.py <proot> <rootfs>
"""
import os
import pty
import select
import sys
import time


def main():
    proot, rootfs = sys.argv[1], sys.argv[2]
    argv = [proot, "-R", rootfs, "/bin/sh", "-c", "echo pty-hello; exit 0"]
    master, slave = pty.openpty()
    pid = os.fork()
    if pid == 0:
        os.setsid()
        for fd in (0, 1, 2):
            os.dup2(slave, fd)
        os.execvp(argv[0], argv)
        os._exit(127)

    data = b""
    deadline = time.time() + 5
    while time.time() < deadline:
        r, _, _ = select.select([master], [], [], 0.2)
        if r:
            try:
                chunk = os.read(master, 4096)
            except OSError:
                break
            if not chunk:
                break
            data += chunk
        # child finished?
        done, _ = os.waitpid(pid, os.WNOHANG)
        if done:
            # drain any remaining output
            try:
                while True:
                    r, _, _ = select.select([master], [], [], 0.1)
                    if not r:
                        break
                    data += os.read(master, 4096)
            except OSError:
                pass
            break
    sys.stdout.write(data.decode(errors="replace"))


if __name__ == "__main__":
    main()
