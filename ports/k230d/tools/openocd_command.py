#!/usr/bin/env python3
"""Run explicit OpenOCD TCL commands, stopping on the first TCL error."""
import argparse
import socket


def run(sock, command, verbose=True):
    # Hex encoding preserves all Tcl metacharacters in the caller's command.
    encoded = command.encode().hex()
    wrapper = (f'set rc [catch {{eval [binary format H* {encoded}]}} result]; '
               'format "%d\\n%s" $rc $result')
    sock.sendall(wrapper.encode() + b'\x1a')
    response = bytearray()
    while not response.endswith(b'\x1a'):
        block = sock.recv(4096)
        if not block:
            raise RuntimeError('OpenOCD closed the connection')
        response.extend(block)
    status, result = response[:-1].decode().split('\n', 1)
    if verbose:
        print(f'> {command}\n{result}', flush=True)
    if status != '0':
        raise RuntimeError(f'OpenOCD command failed: {command}')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=6666)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('command', nargs='+')
    args = parser.parse_args()
    with socket.create_connection(('127.0.0.1', args.port), timeout=args.timeout) as sock:
        for command in args.command:
            run(sock, command)


if __name__ == '__main__':
    main()
