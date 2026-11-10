# HTTPS experiment, 2026-10-27

This is an experimental, explicitly authorized test on one installed PC1864,
not a release or a claim of production stability. It does not replace the
default HTTP examples or establish compatibility with other installations.

`tools/prepare_https_physical.py` creates a private configuration and overrides
of the pinned ESPHome web adapter under `.local/physical-test/https/`. It uses
ESP-IDF's HTTPS server, retains the DSC application and native API, adjusts the
web write origin to HTTPS and leaves encrypted ESPHome OTA separate. The copied
ESPHome adapter, keys, certificates, build output and configuration are local
artifacts; do not commit them. The generator refuses to overwrite an existing
test CA: preserve the current CA before making a new test certificate. The CA
signing key is not retained, and a new generation creates a new CA.

The first build compiled but its TLS handshake failed with mbedTLS error
`-0x7100`. Its SDK configuration enabled the TLS client role only. The previous
physical firmware was restored using encrypted OTA and its HTTP dashboard and
Keybus connection were verified. The revised build requests both mbedTLS TLS
roles and was uploaded after a fresh encrypted API identity and panel-state check.

On the revised build, a client validating the private test CA retrieved the
dashboard, its assets and the panel state through HTTPS. Unauthenticated and
incorrectly authenticated requests returned 401; a cross-origin write was
rejected, a same-origin invalid command was rejected as invalid, and the client
rejected the certificate without the test CA. HTTP on port 80 was unreachable.
An encrypted OTA upload of the same build succeeded; after reboot the encrypted
native API and HTTPS returned, reporting Keybus connected and partition disarmed.
No arm, disarm, or physical keypad command was sent during these checks.

These are short functional checks, not a resource, contention, endurance,
certificate rotation, recovery or security audit. A successful compile and OTA
do not show that TLS handshakes and panel timing remain stable over time. The
installed configuration shares ESPHome's web server with the fallback captive
portal. Switching that server to HTTPS may prevent automatic captive portal
discovery and recovery; that path was not tested. Keep the previous firmware and
a serial recovery path. The test certificate and CA have limited validity. The test CA must be
trusted by each client to avoid browser warnings, and its use is not an
automated certificate provisioning or renewal scheme.
