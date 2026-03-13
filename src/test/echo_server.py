#!/usr/bin/env python3
"""Simple TCP echo server for QEMU guestfwd testing."""
import socket
import sys

HOST = '127.0.0.1'
PORT = 17777

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind((HOST, PORT))
s.listen(1)
s.settimeout(30)
print(f'Echo server listening on {HOST}:{PORT}', flush=True)

try:
    conn, addr = s.accept()
    print(f'Accepted connection from {addr}', flush=True)
    conn.settimeout(30)
    while True:
        data = conn.recv(1024)
        if not data:
            break
        conn.sendall(data)
        print(f'Echoed {len(data)} bytes: {data!r}', flush=True)
    conn.close()
except Exception as e:
    print(f'Echo server: {e}', flush=True)
finally:
    s.close()
    print('Echo server stopped', flush=True)
