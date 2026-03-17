#!/usr/bin/env python3
"""Simple TCP echo server for QEMU guestfwd testing.

Accepts multiple sequential connections and echoes data on each.
"""
import socket
import sys

HOST = '127.0.0.1'
PORT = 17777

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind((HOST, PORT))
s.listen(4)
s.settimeout(120)
print(f'Echo server listening on {HOST}:{PORT}', flush=True)

try:
    while True:
        try:
            conn, addr = s.accept()
        except socket.timeout:
            print('Echo server: accept timed out, stopping', flush=True)
            break
        print(f'Accepted connection from {addr}', flush=True)
        conn.settimeout(30)
        try:
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                conn.sendall(data)
                print(f'Echoed {len(data)} bytes', flush=True)
        except Exception as e:
            print(f'Echo server connection: {e}', flush=True)
        finally:
            conn.close()
            print(f'Connection from {addr} closed', flush=True)
except Exception as e:
    print(f'Echo server: {e}', flush=True)
finally:
    s.close()
    print('Echo server stopped', flush=True)
