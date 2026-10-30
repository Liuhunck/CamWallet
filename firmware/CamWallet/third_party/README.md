# Third-party sources

The QR dependencies are vendored, so building CamWallet does not download them.
Their source files are unmodified. Integration and configuration live in `../qr/`.

## Signing primitives

- `secp256k1/`: bitcoin-core/libsecp256k1 **v0.8.0**, commit
  `6e2c8bc4ecdc6e71dbe7a368f360d8d453ce435d`.
  Source: https://github.com/bitcoin-core/secp256k1/tree/6e2c8bc4ecdc6e71dbe7a368f360d8d453ce435d
  Includes upstream public API headers, internal headers, `secp256k1.c`, the
  two precomputed table sources and recovery module. MIT license in `COPYING`.
  Source content is unchanged; `../crypto/CMakeLists.txt` compiles the portable
  32-bit path (`USE_FORCE_WIDEMUL_INT64`), recovery, 22 KiB signing table, and
  verification window 4. No experimental ARM assembly or extra signing schemes.
  The wrapper uses a checked 1 KiB preallocated context and never calls the
  heap-allocating context API. Host tests use the same arithmetic configuration.
- `trezor_crypto/`: selected files from trezor/trezor-firmware `crypto/`, commit
  `76bfa7f15470409733c92e89d5cc0bb55c23463a`.
  Source: https://github.com/trezor/trezor-firmware/tree/76bfa7f15470409733c92e89d5cc0bb55c23463a/crypto
  Unmodified SHA2, SHA3/Keccak, HMAC, RIPEMD160, memzero and their headers/config;
  upstream `LICENSE` preserves component notices. No BIP39 or restore code is
  included. CamWallet's BIP32 fixed-path wrapper uses these HMAC/hash primitives
  and libsecp256k1's public-key/tweak API, checked against Python HD derivation.
- The RAM-only test account obtains seed/blinding material from Pico SDK 2.3.1
  `pico_rand` on RP2350. Hardware entropy qualification, side-channel evaluation,
  persistent keys and recovery are not implemented. Firmware signatures are
  restricted to Sepolia (11155111), with explicit manual approval.

## cUR transport

- Upstream: https://github.com/odudex/cUR
- Commit: `33d8a6742c37d0d9bd19bdb65269f238d9a024d3`
- Copied unchanged: top-level `src/*.{c,h}`, `src/sha256/`, and `LICENSE`.
  No `src/types/` Bitcoin payload codecs or Rust dependencies are included.
- License: BSD-2-Clause Plus Patent; see `cur/LICENSE` and
  `../THIRD_PARTY_NOTICES.txt`. Bundled SHA-256 retains its source notices.
- Integration lives in `../transport/`. It compiles only the generic envelope,
  Bytewords, Fountain, CRC and bundled SHA-256 sources. Defaults remain the
  portable small CRC and XOR paths; interoperability math stays double.
- Compiler limits: `UR_MAX_SEQ_LEN=32`, `UR_MAX_MESSAGE_LEN=768`.
  The forced `cur_memory.h` include redirects **all** upstream malloc/calloc/
  realloc/free calls into a 16 KiB static arena; upstream sources are not patched.
  The wrapper caps QR text at 128 bytes and output fragments at 24 bytes.
- cUR knows no EVM business rules. `../evm/` implements the supported ERC-4527
  subset; further chain codecs can use the same transport wrapper.

## quirc

- Upstream: https://github.com/dlbeer/quirc
- Commit: `f2e4271ae43a15d9263704e07bc86cba5697d74d`
- Copied from `lib/`: `quirc.h`, `quirc_internal.h`, `quirc.c`, `identify.c`,
  `decode.c`, `version_db.c`; also the upstream `LICENSE`.
- License: ISC; retain `quirc/LICENSE` and the source headers.
- CamWallet sets `QUIRC_FLOAT_TYPE=float` and `QUIRC_USE_TGMATH` through CMake.
  Default region limits are retained to share the image/region-pixel allocation.

## Nayuki QR Code generator

- Upstream: https://github.com/nayuki/QR-Code-generator
- Commit: `3c6d0b3cefb4e049dc337e82237c9644399716a8`
- Copied from `c/`: `qrcodegen.c`, `qrcodegen.h`.
- License: MIT; the complete license is retained in both source headers.
- CamWallet limits its own output to QR versions 1–9 and a 128-byte payload.

## Waveshare

See `../THIRD_PARTY_NOTICES.txt` for the original archive URL, its SHA-256,
and the notices for the board driver adaptations, sensor register table and font.
