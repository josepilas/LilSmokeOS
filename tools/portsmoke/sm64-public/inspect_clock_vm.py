#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Inspect only the private clock-QA VM via its existing QMP connection.

Linux x86-64 pidfd_getfd duplicates the test controller's monitor descriptor.
Only that verified, sleeping controller is paused briefly to serialize QMP.
The guest CPU is not stopped. Optional SysRq-W uses unmapped right Alt and
only asks the private Linux guest to print blocked tasks; never host keys.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import platform
import signal
import socket
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--blocked-tasks', action='store_true')
    args = parser.parse_args()
    out = args.out.resolve()
    if (not str(out).startswith(('/root/sm64-prep3-clock-', '/root/sm64-prep4-resume-vm-',
                                '/root/sm64-prep5-resume-vm-', '/root/sm64-prep6-resume-vm-',
                                '/root/sm64-prep7-resume-vm-'))
            or platform.machine() != 'x86_64'):
        raise ValueError('Not this isolated x86-64 host QA')
    matches = []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdecimal(): continue
        try:
            command = (proc / 'cmdline').read_bytes().split(b'\0')
            if (b'tools/portsmoke/sm64-public/clock_vm_test.py' in command and
                    os.fsencode(str(out)) in command):
                matches.append(proc)
        except OSError:
            continue
    if len(matches) != 1: raise RuntimeError('Expected one isolated test controller')
    proc = matches[0]
    pid = int(proc.name)
    for _ in range(30):
        if (proc / 'wchan').read_text().strip() == 'hrtimer_nanosleep': break
        time.sleep(0.1)
    else: raise RuntimeError('Controller is not idle between QMP operations')
    sockets = [int(fd.name) for fd in (proc / 'fd').iterdir()
               if os.readlink(fd).startswith('socket:')]
    if len(sockets) != 1: raise RuntimeError('Ambiguous controller monitor descriptor')
    os.kill(pid, signal.SIGSTOP)
    descriptor = os.pidfd_open(pid)
    try:
        libc = ctypes.CDLL(None, use_errno=True)
        copied = libc.syscall(438, descriptor, sockets[0], 0)
        if copied < 0: raise OSError(ctypes.get_errno(), 'pidfd_getfd')
        with socket.socket(fileno=copied) as monitor:
            monitor.settimeout(5)
            incoming = monitor.makefile('rb')
            def call(name, arguments=None):
                request = dict(execute=name, id='private-diagnostic')
                if arguments is not None: request['arguments'] = arguments
                monitor.sendall((json.dumps(request) + '\n').encode())
                while True:
                    reply = json.loads(incoming.readline())
                    if reply.get('id') == 'private-diagnostic': return reply
                    if 'event' not in reply: raise RuntimeError('Unexpected outstanding QMP response')
            result = dict(status=call('query-status'), blocks=call('query-block'))
            result['registers'] = call('human-monitor-command', dict(**{'command-line': 'info registers'}))
            result['screenshot'] = call('screendump', dict(filename=str(out / 'diagnostic.ppm')))
            if args.blocked_tasks:
                result['blocked_tasks'] = call('send-key', dict(keys=[
                    dict(type='qcode', data=key) for key in ('alt_r', 'sysrq', 'w')],
                    **{'hold-time': 300}))
            print(json.dumps(result, indent=2))
            incoming.close()
    finally:
        os.close(descriptor)
        os.kill(pid, signal.SIGCONT)


if __name__ == '__main__': main()
