#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
# SPDX-License-Identifier: LGPL-2.1-only

"""Send ML-DSA-44 verification requests to the RIOT server over TCP/IPv6.

The signature either comes from a file (--sig) or is created on the fly with
the host tool (--sk, calls ./mldsa_tool sign). Only the standard library is
used.
"""

import argparse
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

MAGIC = b"MLDS"
VERSION = 1
ALG_MLDSA44 = 1
SIG_BYTES = 2420
REQ_HDR = struct.Struct(">4sBBBBHH")
RSP_HDR = struct.Struct(">4sBBIH")
STATUS = {0: "OK", 1: "BAD_SIG", 2: "BAD_REQUEST", 3: "UNSUPPORTED"}
CONNECT_RETRIES = 5
CONNECT_RETRY_DELAY = 0.3


def _load_message(spec):
    if spec.startswith("@"):
        with open(spec[1:], "rb") as f:
            return f.read()
    return spec.encode()


def _sign(tool, sk, msg):
    with tempfile.TemporaryDirectory() as tmp:
        msg_path = os.path.join(tmp, "msg.bin")
        sig_path = os.path.join(tmp, "sig.bin")
        with open(msg_path, "wb") as f:
            f.write(msg)
        subprocess.run([tool, "sign", sk, msg_path, sig_path], check=True,
                       stdout=subprocess.DEVNULL)
        with open(sig_path, "rb") as f:
            return f.read()


def _connect(addr, iface, port, timeout, mss):
    info = socket.getaddrinfo(f"{addr}%{iface}", port, socket.AF_INET6,
                              socket.SOCK_STREAM)
    last = None
    for _ in range(CONNECT_RETRIES):
        sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        if mss:
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_MAXSEG, mss)
        try:
            sock.connect(info[0][4])
            return sock
        except (ConnectionRefusedError, socket.timeout, OSError) as exc:
            last = exc
            sock.close()
            # the server has a single connection slot that takes a moment to
            # return to LISTEN after the previous request
            time.sleep(CONNECT_RETRY_DELAY)
    raise last


def _request(args, msg, sig):
    hdr = REQ_HDR.pack(MAGIC, VERSION, ALG_MLDSA44, 0, 0, len(msg), len(sig))
    if args.bad_header:
        hdr = b"NOPE" + hdr[4:]
    sock = _connect(args.addr, args.iface, args.port, args.timeout, args.mss)
    try:
        t0 = time.monotonic()
        sock.sendall(hdr + msg + sig)
        rsp = sock.recv(RSP_HDR.size, socket.MSG_WAITALL)
        rtt = time.monotonic() - t0
    finally:
        sock.close()
    if len(rsp) != RSP_HDR.size:
        raise RuntimeError(f"short response ({len(rsp)} bytes)")
    magic, status, _, verify_us, msg_len = RSP_HDR.unpack(rsp)
    if magic != MAGIC:
        raise RuntimeError(f"bad response magic {magic!r}")
    return status, verify_us, msg_len, rtt


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--addr", default="fe80::ff:fe00:1", help="board IPv6 address")
    p.add_argument("--iface", default="enp0s31f6", help="host interface for link-local scope")
    p.add_argument("--port", type=int, default=7777)
    p.add_argument("--msg", default="hello", help="message text, or @file")
    sig_src = p.add_mutually_exclusive_group()
    sig_src.add_argument("--sig", help="signature file")
    sig_src.add_argument("--sk", default="../demo-keys/sk.bin",
                         help="secret key, signs with the host tool (default)")
    p.add_argument("--tool", default=os.path.join(os.path.dirname(__file__) or ".", "mldsa_tool"))
    p.add_argument("--tamper", action="store_true", help="flip one signature bit")
    p.add_argument("--bad-header", action="store_true", help="send a wrong magic")
    p.add_argument("--count", type=int, default=1, help="repeat the request")
    p.add_argument("--timeout", type=float, default=10.0, help="socket timeout in seconds")
    p.add_argument("--mss", type=int, default=0, help="limit the TCP segment size (debugging)")
    args = p.parse_args()

    msg = _load_message(args.msg)
    if args.sig:
        with open(args.sig, "rb") as f:
            sig = f.read()
    else:
        sig = _sign(args.tool, args.sk, msg)
    if len(sig) != SIG_BYTES:
        sys.exit(f"signature is {len(sig)} bytes, expected {SIG_BYTES}")
    if args.tamper:
        sig = bytearray(sig)
        sig[len(sig) // 2] ^= 0x01
        sig = bytes(sig)

    expected = 2 if args.bad_header else (1 if args.tamper else 0)
    matched = 0
    verify_times = []
    rtts = []
    for i in range(args.count):
        status, verify_us, msg_len, rtt = _request(args, msg, sig)
        name = STATUS.get(status, f"status {status}")
        print(f"[{i + 1}/{args.count}] {name} msg_len={msg_len} "
              f"verify={verify_us} us rtt={rtt * 1e3:.1f} ms")
        matched += (status == expected)
        if status in (0, 1):
            verify_times.append(verify_us)
        rtts.append(rtt * 1e3)

    if args.count > 1:
        print(f"{matched}/{args.count} responses as expected ({STATUS[expected]})")
        if verify_times:
            print(f"verify: min {min(verify_times)} us, "
                  f"avg {sum(verify_times) // len(verify_times)} us, max {max(verify_times)} us")
        print(f"rtt: min {min(rtts):.1f} ms, avg {sum(rtts) / len(rtts):.1f} ms, "
              f"max {max(rtts):.1f} ms")
    sys.exit(0 if matched == args.count else 1)


if __name__ == "__main__":
    main()
