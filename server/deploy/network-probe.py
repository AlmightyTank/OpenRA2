#!/usr/bin/env python3
"""S0.1F real-daemon loopback protocol probe (stdlib only).

Exercises TCP hello/create/join/close and bidirectional V3 UDP forwarding.
Run ONLY against an isolated disposable loopback lobby started for testing.
It creates and closes a test room; never point at a live player server.
"""
import json
import socket
import struct
import sys
import time

HOST = "127.0.0.1"
CONTROL_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 50175
V3_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 50176
V3 = (HOST, V3_PORT)


def tcp_client(nick, pid):
    sock = socket.create_connection((HOST, CONTROL_PORT), timeout=5)
    sock.settimeout(5)
    stream = sock.makefile("rb")
    send(sock, {
        "t": "hello", "magic": "RA2N", "proto": 2, "ver": "0.9.0",
        "sim": 1, "rules": "1234abcd", "plat": 1,
        "pid": pid, "nick": nick,
    })
    assert receive(stream)["t"] == "hello_ok", "TCP handshake failed"
    return sock, stream


def send(sock, message):
    sock.sendall(json.dumps(message, separators=(",", ":")).encode() + b"\n")


def receive(stream):
    line = stream.readline()
    if not line:
        raise AssertionError("unexpected TCP disconnect")
    return json.loads(line)


def make_socket():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2)
    sock.bind((HOST, 0))
    return sock


def packet(sender, receiver, content=b""):
    return struct.pack("<II", sender, receiver) + content


def expect_udp(sender_socket, expected):
    for _ in range(4):
        try:
            data, origin = sender_socket.recvfrom(2048)
            if origin == V3 and data == expected:
                return
        except socket.timeout:
            pass
    raise AssertionError("V3 forward missing or corrupted")


def main():
    host = guest = None
    ha = ga = None
    hstream = gstream = None
    try:
        host, hstream = tcp_client("SmokeHost", "0000000000abc001")
        guest, gstream = tcp_client("SmokeGuest", "0000000000abc002")
        send(host, {"t": "create", "name": "Smoke disposable", "max": 2, "v3": 98760001})
        created = receive(hstream)
        assert created["t"] == "created"
        code = created["code"]
        assert len(code) == 6
        send(guest, {"t": "join", "code": code, "v3": 98760002, "relay": "v3"})
        joined = receive(gstream)
        assert joined["t"] == "joined" and joined["code"] == code
        peer = receive(hstream)
        assert peer["t"] == "peer" and peer["v3"] == 98760002

        ha, ga = make_socket(), make_socket()
        # V3 sender registration uses a reserved (nonexistent) destination.
        ha.sendto(packet(98760001, 0xFFFFFFFE), V3)
        ga.sendto(packet(98760002, 0xFFFFFFFE), V3)
        time.sleep(0.10)
        up = packet(98760002, 98760001, b"guest-packet")
        ga.sendto(up, V3)
        expect_udp(ha, up)
        down = packet(98760001, 98760002, b"host-packet")
        ha.sendto(down, V3)
        expect_udp(ga, down)

        send(host, {"t": "close"})
        closed = receive(gstream)
        assert closed["t"] == "room_closed" and closed["code"] == code
        print("PASS: TCP hello/create/join/close, room isolation and bidirectional V3 UDP")
    finally:
        for sock in (ha, ga, host, guest):
            if sock is not None:
                sock.close()
        for stream in (hstream, gstream):
            if stream is not None:
                stream.close()


if __name__ == "__main__":
    main()
