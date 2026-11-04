# ML-DSA-44 verify server

A TCP/IPv6 server that verifies post-quantum signatures. A client sends
`message + signature`, the board verifies the signature with
[ML-DSA-44](https://csrc.nist.gov/pubs/fips/204/final) (FIPS 204, the
PQClean "clean" implementation from the `pqclean` package) against a public
key compiled into the firmware, and answers with the result and the time the
verification took. The result is also shown on the LEDs and printed on the
console.

The example was written for the Gaisler NOEL-V boards (`arty-a7-noelv`,
`zedboard-noelv`), but it only needs a board with an Ethernet interface
supported by `netdev_default` and enough RAM for a 64 KiB thread stack.

## Wire protocol

Both directions start with a 12 byte header. All multi-byte fields are
big-endian.

Request:

| Field | Size | Value |
|-------|------|-------|
| magic | 4 | `"MLDS"` |
| version | 1 | 1 |
| alg | 1 | 1 = ML-DSA-44 |
| flags | 1 | 0 |
| reserved | 1 | 0 |
| msg_len | 2 | length of the message, at most `MLDSA_SERVER_MAX_MSG_LEN` (1024) |
| sig_len | 2 | length of the signature, must be 2420 |
| msg, sig | var | message followed by the signature |

Response:

| Field | Size | Value |
|-------|------|-------|
| magic | 4 | `"MLDS"` |
| status | 1 | 0 OK, 1 BAD_SIG, 2 BAD_REQUEST, 3 UNSUPPORTED |
| reserved | 1 | 0 |
| verify_time_us | 4 | time of the verification in microseconds (0 if not run) |
| msg_len | 2 | echo of the request |

The client is expected to close the connection after it has read the
response. The server handles one connection at a time and drains whatever
the client still sent before it closes, so a malformed request does not
leave stale bytes behind for the next connection.

LEDs: LED0 is on while the server listens and toggles on every request, LED1
is on during a verification, LED2 shows that the last signature was valid,
LED3 that it was not. `MLDSA_SERVER_USE_LEDS=0` leaves the LEDs alone.

## Files

- `main.c`: the server and the shell commands `mldsa_stats` and
  `mldsa_selftest`
- `include/mldsa_pubkey.h`: the public key (generated)
- `include/mldsa_testvec.h`: a known-good message and signature for
  `mldsa_selftest` (generated)
- `demo-keys/`: the demo key pair the headers were generated from. It is
  public, use it for demonstrations only.
- `host/mldsa_tool.c`: host-side key generation, signing and header export,
  built from the same PQClean sources as the firmware
- `host/mldsa_client.py`: host-side client (Python 3, standard library only)

## Building and running

```shell
make BOARD=arty-a7-noelv flash term
```

The console shows the link-local address of the board, for the NOEL-V
boards `fe80::ff:fe00:1`, and `listening on [::]:7777`.

The TCP port and the largest accepted message can be changed with
`MLDSA_SERVER_PORT` and `MLDSA_SERVER_MAX_MSG_LEN`.

### Arty A7: the link runs at 10 Mbit/s

The GRETH of the prebuilt NOEL-ARTYA7 design (40 MHz AHB, DDR3 behind it)
cannot drain its receive FIFO at 100 Mbit/s. Frames longer than about
260 bytes are dropped by the MAC with the overrun bit (`OE`) set in the
descriptor, which is invisible to the network stack and only shows with the
driver's debug output. The example therefore builds with
`CONFIG_GRETH_ADVERTISE_10M_ONLY=1` for `arty-a7-noelv`, so the PHY
negotiates 10BASE-T. Measured on the board:

| Link | ping payload 56 B | 200 B | 400 B .. 1400 B |
|------|-------------------|-------|-----------------|
| 100 Mbit/s | 0 % loss | 40 % loss | 100 % loss |
| 10 Mbit/s | 0 % | 0 % | 0 % |

During these tests the USB-powered board also browned out several times
(the FT2232 dropped off the USB bus and the FPGA reloaded the factory image),
after seconds at 100 Mbit/s and after a few minutes at 10 Mbit/s. Power the
board from the barrel jack for longer runs.

## Host tool and client

RIOT checks PQClean out into `build/pkg/pqclean` on the first firmware
build. The host tool is built from that checkout:

```shell
cd host
make
```

Sign and send a message with the demo key (the interface is the host side of
the Ethernet link, link-local addresses need it):

```shell
./mldsa_client.py --iface enp0s31f6 --msg "hello"
./mldsa_client.py --iface enp0s31f6 --msg "hello" --tamper        # expect BAD_SIG
./mldsa_client.py --iface enp0s31f6 --msg "hello" --bad-header    # expect BAD_REQUEST
./mldsa_client.py --iface enp0s31f6 --msg "hello" --count 20      # timing statistics
```

A pre-made signature file can be sent with `--sig`, `--mss` limits the TCP
segment size for experiments. On the board, `mldsa_stats` prints the
counters and `mldsa_selftest` verifies the built-in test vector.

To use a new key pair, regenerate the keys and the two headers and rebuild
the firmware:

```shell
make -C host keys
make BOARD=arty-a7-noelv flash
```

## Interoperability with OpenSSL

OpenSSL 3.5 implements ML-DSA independently of PQClean. Its signatures use
the same pure ML-DSA mode with an empty context string, so they verify on
the board. The raw public key is the last 1312 bytes of the DER encoded
SubjectPublicKeyInfo:

```shell
openssl genpkey -algorithm ML-DSA-44 -out ossl_key.pem
openssl pkey -in ossl_key.pem -pubout -outform DER | tail -c 1312 > ossl_pk.bin
./mldsa_tool export-header ossl_pk.bin ../include/mldsa_pubkey.h
make -C .. BOARD=arty-a7-noelv flash

printf 'hello' > msg.bin
openssl pkeyutl -sign -rawin -in msg.bin -inkey ossl_key.pem -out ossl_sig.bin
./mldsa_client.py --iface enp0s31f6 --msg @msg.bin --sig ossl_sig.bin    # expect OK
```

The other direction works as well: a signature made by `mldsa_tool sign`
verifies with `openssl pkeyutl -verify -rawin -pubin` once the raw public
key is wrapped into a SubjectPublicKeyInfo (both directions were checked on
the host with OpenSSL 3.5.9).

## Measurements

`tests/pkg/pqclean` times key generation, signing and verification without
networking. Arty A7-100T, NOEL-V GP32L-SC (rv32imac, 40 MHz), `-Os`,
5 rounds:

| Operation | min | avg | max |
|-----------|-----|-----|-----|
| keygen | 48.0 ms | 48.8 ms | 51.0 ms |
| sign | 110.2 ms | 237.8 ms | 707.2 ms |
| verify | 51.8 ms | 51.9 ms | 52.0 ms |

Signing time varies because ML-DSA rejects and retries candidate signatures.
For comparison, the same code verifies in about 0.12 ms on a desktop
(`BOARD=native`).

Server on the same board, link at 10 Mbit/s, 26 requests with 5 to 1024 byte
messages:

| | value |
|-|-------|
| verification | 59.3 ms min, 59.6 ms avg, 61.9 ms max |
| request round trip seen by the client | 70.1 ms min, 71.1 ms avg, 76.1 ms max |
| server thread stack high-water mark | 36.3 KiB of 64 KiB |
| main thread (shell + `mldsa_selftest`) | 38.8 KiB of 64 KiB |
| `gnrc_tcp` thread | 508 B of 1024 B |
| firmware size | 72 KiB text, 17 KiB data, 178 KiB bss |

Verification in the server takes about 8 ms longer than in the standalone
test because the network threads and their interrupts run alongside it.
