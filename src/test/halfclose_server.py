#!/usr/bin/env python3
"""TCP half-close test server: sends data then closes its end first.

The server sends a known payload, then shuts down its write side (FIN),
but keeps the socket open for a moment to verify the client ACKs correctly.
This exercises the CLOSE_WAIT path on the client (Sodex) side.
"""
import socket
import sys

HOST = '127.0.0.1'
PORT = 17778

PAYLOAD = b'HALFCLOSE_TEST_OK\n'

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind((HOST, PORT))
s.listen(4)
s.settimeout(120)
print(f'Half-close server listening on {HOST}:{PORT}', flush=True)

try:
    while True:
        try:
            conn, addr = s.accept()
        except socket.timeout:
            print('Half-close server: accept timed out, stopping', flush=True)
            break
        print(f'Accepted connection from {addr}', flush=True)
        conn.settimeout(30)
        try:
            # Send payload, then close write side (sends FIN)
            conn.sendall(PAYLOAD)
            print(f'Sent {len(PAYLOAD)} bytes, shutting down write side', flush=True)
            conn.shutdown(socket.SHUT_WR)

            # Wait for client to close its side (read until EOF)
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                print(f'Received {len(data)} bytes after shutdown (unexpected)', flush=True)
            print(f'Client closed connection', flush=True)
        except Exception as e:
            print(f'Half-close server connection: {e}', flush=True)
        finally:
            conn.close()
            print(f'Connection from {addr} fully closed', flush=True)
except Exception as e:
    print(f'Half-close server: {e}', flush=True)
finally:
    s.close()
    print('Half-close server stopped', flush=True)
