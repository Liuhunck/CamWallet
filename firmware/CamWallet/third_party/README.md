# Third-party sources

The QR dependencies are vendored, so building CamWallet does not download them.
Their source files are unmodified. Integration and configuration live in `../qr/`.

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
