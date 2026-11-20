# Licensing and distribution audit

- **Glon source:** no root `LICENSE` file exists in the repository at the
  release commit. The source is published by the repository copyright holder;
  no additional licence text is invented here.
- **CivetWeb 1.16:** MIT; full text at `third_party/civetweb/LICENSE.md`.
- **OpenSSL 3:** Apache-2.0; full text at `release/OPENSSL-APACHE-2.0.txt`.
  Linux links to the system package; Windows HTTPS packages include the
  OpenSSL DLLs from Git for Windows.
- **Windows executables:** statically linked MSVC C runtime (`/MT`); no
  redistributable installer is required.
- **Examples/assets:** original synthetic examples in this repository; no
  third-party images or proprietary assets are included.
- **Excluded:** private keys, production credentials, personal data, private
  OpenViking memory, local agent credentials.

No legal assurance beyond the stated third-party licences is made.
