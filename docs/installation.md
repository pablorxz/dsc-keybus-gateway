# Install DSC Keybus Gateway

This guide uses a DSC PowerSeries PC1864, a classic ESP32 DevKitC-compatible
board, and a SparkFun BOB-12009 level converter. The operator of one PC1864
installation confirmed that the keypad and Home Assistant integration are
working. Treat that installation as a reference for your own setup. See the
[physical validation record](physical-validation.md) for the tests performed
and their scope.

## 1. Identify the wiring

Check the panel terminals, converter model, and ESP32 GPIO labels against the
[hardware reference](hardware.md), which includes a diagram and power options.
If your ESP32 module uses an external antenna, connect a compatible antenna.
Disarm the panel and disconnect both its AC supply and its battery before
changing the wiring. If you cannot safely isolate the power, ask an alarm
technician.

| PC1864 | BOB-12009 | ESP32 |
| --- | --- | --- |
| `YEL` (clock) | `HV1` to `LV1` through the converter | GPIO22 |
| `GRN` (data) | `HV2` to `LV2` through the converter | GPIO21 |
| `BLK` (ground) | `GND` | GND |
| — | `LV` | 3V3 |

Power the ESP32 through USB from a suitable 5 V supply. The grounds must remain
common with this circuit. Follow the project-specific circuit in the hardware
reference. Connecting the Keybus signals directly to the GPIOs, bridging the
converter's paired signal pads, or feeding panel auxiliary voltage into the
ESP32 can damage the hardware.

## 2. Prepare a local configuration

Start from the source for the [release you want to install](https://github.com/pablorxz/dsc-keybus-gateway/releases).
From its root directory, create a Python 3.12 virtual environment and install
the pinned ESPHome requirements.

```sh
python3.12 -m venv .venv-esphome
source .venv-esphome/bin/activate
python -m pip install -r requirements/firmware.txt
```

On Windows, run `py -3.12 -m venv .venv-esphome` and use
`.\.venv-esphome\Scripts\python.exe` in place of `python` in the commands below,
including the requirements installation.

Create `.local/physical-test/secrets.yaml` with your own values. Generate a
fresh API encryption key with
`python -c "import base64, secrets; print(base64.b64encode(secrets.token_bytes(32)).decode())"`.
Keep the key local and use a fallback Wi-Fi password of at least eight characters.

```yaml
wifi_ssid: "YOUR_WIFI_SSID"
wifi_password: "YOUR_WIFI_PASSWORD"
fallback_ap_password: "YOUR_FALLBACK_WIFI_PASSWORD"
api_encryption_key: "YOUR_BASE64_ENCRYPTION_KEY"
web_username: "YOUR_DASHBOARD_USERNAME"
web_password: "YOUR_DASHBOARD_PASSWORD"
```

Create `.local/physical-test/zones.yaml` to select the zones for your own panel.
For example, the following adds generic labels to zones 1 and 2:

```yaml
visible_zones: [1, 2]
```

Add display names, groups, or sensor types as described in
[zone configuration](zones.md). Without a local zone file the example selects
zones 1–10 for demonstration. Render the configuration with:

```sh
python tools/prepare_physical.py
```

Open the generated `.local/physical-test/physical-full.yaml`. Check the
`esp32.board`, Keybus pin assignments, and alarm partition against your hardware.
Remove `write_pin: 21` for the first boot so the gateway starts in read-only
mode. The renderer overwrites this file when run again. Keep the configuration
and firmware binaries private because they can contain installation details or
credentials.

## 3. Build, flash, and check the keypad

Use a USB data cable with the ESP32 disconnected from the Keybus. Confirm the
ESP32's serial port and replace `COM5` below with that port, such as
`/dev/ttyUSB0` on Linux. The first command validates and builds the firmware.

```sh
python -m esphome compile .local/physical-test/physical-full.yaml
python -m esphome upload .local/physical-test/physical-full.yaml --device COM5
```

Unplug the ESP32 and connect it to the powered-off panel using the checked
wiring. Restore power and verify the physical keypad works normally. Find the
ESP32 address in your router's DHCP leases or the ESPHome serial logs. Check
that the gateway sees the panel and that opening and closing one zone changes
its state. Zones can remain unknown until the panel sends a valid report.

If you want to send commands, restore `write_pin: 21` on the documented
shared-data circuit, rebuild and flash again, then repeat the keypad check.
For network updates and recovery, see [OTA updates](ota.md).

## 4. Add the gateway to Home Assistant

1. In **Settings → Devices & services**, configure the discovered ESPHome
   device. If it does not appear, select **Add integration → ESPHome**.
2. Enter the ESP32 hostname or IP address and TCP port `6053` when prompted.
   If Home Assistant runs on a separate network, allow it to reach that port.
3. Enter `api_encryption_key` from your local `secrets.yaml` as the encryption
   key (Noise PSK).
4. Check the alarm control panel, Keybus connection, trouble, fire and configured
   zone entities. Confirm a zone changes when you operate its sensor.
5. With writing enabled, test arm and disarm while you have access to the
   physical keypad. Use your normal panel access code and verify the observed
   state. If a command's result is uncertain, check the keypad before retrying.

The [Home Assistant ESPHome integration guide](https://www.home-assistant.io/integrations/esphome/)
explains discovery and manual setup. The API key, alarm access code, and
dashboard login serve different purposes. The example dashboard uses HTTP
Digest on port 80 and sends command bodies without encryption. Use Home
Assistant's encrypted ESPHome connection for control, and keep the dashboard
off the public internet.
