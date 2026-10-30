# Vendored Python UR reference

- Upstream: https://github.com/Foundation-Devices/foundation-ur-py
- Commit: `a371f6355fb3433cc989ad5bf28f87a347b222fe`
- Copied without changes: `ur/*.py` and `LICENSE` into `foundation_ur/`.
- Main license: BSD-2-Clause Plus Patent, Copyright 2020 Foundation Devices.
  Keep `foundation_ur/LICENSE` and all source notices when redistributing.
- `cbor_lite.py` also carries the Isode CBOR-lite MIT attribution and original
  source/copyright URL. `xoshiro256.py` retains the authors' public-domain notice.
- This is a Python implementation independent of the cUR firmware library.
  No Python extension, Rust toolchain, or additional pip package is needed.
- `ur_protocol.py` adds input size/fragment limits before passing camera data
  into the reference decoder. The reference package itself is left unmodified.
