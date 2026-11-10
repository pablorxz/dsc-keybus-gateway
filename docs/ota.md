# OTA updates

This page describes the ESPHome OTA mechanism used by the example configurations.
It is not authorization to update an installed alarm. For isolated-device setup
and identity checks, follow [the bench guide](bench.md).

## How it works

1. A computer builds a firmware image from the local sources and ESPHome YAML.
2. An authorized uploader connects to the explicitly identified ESP32 over Wi-Fi.
3. ESPHome authenticates the uploader and transfers the image. The ESP32 writes
   it to an OTA application partition and restarts to run it.
4. The operator verifies that the expected device reconnects and that its API,
   observed state and any required interfaces work. Upload success alone does not
   prove that Keybus or the alarm works correctly.

The ESP32 bridge is unavailable during its restart. The panel and physical
keypads operate independently, but their behavior during a bridge failure must be
verified for the actual installation.

There is no automatic update service or signed release pipeline in this repository.
The build is produced locally. A firmware binary can contain network credentials
and encryption keys; protect build artifacts as secrets and do not commit them.

## Ports and credentials

The pinned ESPHome examples configure the native API with an encryption key and
ESPHome OTA with `encryption: {}`. In this configuration OTA reuses the API key
and uses ESPHome's authenticated, encrypted Noise transport. The default/explicit
ports are:

| Service | Port | Purpose |
| --- | --- | --- |
| Dashboard | TCP 80 | HTTP with Digest authentication; **not encrypted** |
| ESPHome native API | TCP 6053 | Encrypted device API, including Home Assistant |
| ESPHome OTA | TCP 3232 | Encrypted firmware upload |

Restrict access to authorized hosts. The web username/password, the alarm user
code, and the API/OTA encryption key have different purposes. The API/OTA key
is shared by those two services in this configuration: protect it accordingly.
When rotating the key, the device must authenticate the update with the key it
currently runs. Losing access to that key or to the network can require local
recovery. See [ESPHome OTA documentation](https://esphome.io/components/ota/esphome/)
and [native API documentation](https://esphome.io/components/api/).

## Isolated bench workflow

After preparing an explicitly authorized, physically disconnected bench device
and its ignored manifest and credentials as described in [bench.md](bench.md):

```sh
python tools/prepare_bench.py
python -m esphome compile .local/bench/bench.yaml
python tools/bench.py ota --expected-mac <confirmed-bench-mac>
```

The bench helper accepts only the authorized manifest target, verifies its
identity through the encrypted API, checks that the local binary is not older
than the relevant sources, and uploads to the explicit address. It is designed
for the synthetic bench configuration, not the installed alarm. The helper's
local `ota_password` value is not used by the current encrypted OTA template.

## Recovery and limits

A failed update, bad configuration, loss of Wi-Fi, wrong address or lost key may
make OTA unavailable. ESPHome has a safe-mode recovery mechanism, but it does not
guarantee an automatic rollback or remote recovery in every failure. Keep a
known-good build and a practical serial recovery path before updating a device.
If using a fallback access point or captive portal, review its security and
recovery behavior separately; do not assume the native OTA transport protects
all recovery interfaces.

For the installed alarm, a separate user-authorized operation must first identify
the exact device, confirm the matching configuration and current credentials,
and prepare recovery. The current repository has no general production-upload
helper or tested recovery procedure for every failure. See the limited
[physical validation record](physical-validation.md) and
[ESPHome safe-mode documentation](https://esphome.io/components/safe_mode/).
