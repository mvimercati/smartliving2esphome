# smartliving2esphome

ESPHome firmware that connects an **INIM SmartLiving 1050** alarm panel to **Home Assistant**.

An ESP8266 with an RS232 level shifter sits on the serial port of the panel, polls it and talks to
Home Assistant with the native ESPHome API (encrypted). Zones, areas, faults and the event log become
Home Assistant entities; arming, zone bypass and memory reset are done from Home Assistant with the
user's PIN, which is typed each time and never stored.

```
Home Assistant ──ESPHome API── ESP8266 (D1 mini) ──UART 57600 8E1── RS232 level shifter ── panel RS232 port
```

## What you get

- **Zones**: one binary sensor per zone (window, door, motion...), plus its bypass state.
- **Areas**: one alarm panel per area, with the INIM arm modes: away = *Inserita* (with delays),
  home = *Parziale*, night = *Immediata* (no delay), disarmed = *Disinserita*.
- **Scenarios**: the main "Antifurto" alarm panel arms the scenarios programmed in the panel.
- **Zone bypass**: one lock per zone (locked = protected, unlocked = excluded), "include all".
- **Memories**: alarm and tamper memories, the zones that triggered the alarm, reset with the PIN.
- **Faults and tampers**: one binary sensor per known bit, plus summaries (power, communications, radio).
- **Event log** of the panel, battery and power supply voltage, result of the last command.
- **Converter diagnostics**: WiFi signal, uptime, reset reason, heap, WiFi disconnections.

Entity names are in Italian, as the panel's own texts.

## Repository

| Path | What |
|------|------|
| `esphome/inim.yaml` | device: WiFi, API actions, UART, converter diagnostics |
| `esphome/installation.yaml` | zones (terminals), areas, scenarios of the author's installation |
| `esphome/installation.example.yaml` | a smaller installation to start from |
| `esphome/secrets.example.yaml` | model of `secrets.yaml` (WiFi, API key, fallback AP password; gitignored) |
| `esphome/components/inim/` | ESPHome external component: `protocol.h` (frames, pure), `inim.h/.cpp` (poll loop, commands), `__init__.py` (generates the entities from the installation) |
| `esphome/tests/test_protocol.cpp` | host test of the protocol helpers |
| `homeassistant/inim.yaml`, `inim.example.yaml` | Home Assistant package: alarm panels and locks that ask for the PIN |
| `dashboard/antifurto-view.yaml`, `antifurto-view.example.yaml` | Home Assistant dashboard view |

The `*.example.yaml` files go together (same zones and areas) and use the entity ids Home Assistant
gives a new device. The author's files keep the entity ids of an earlier MQTT bridge
(`default_entity_id` in the package, entity registry renamed once), so they differ.

## Hardware

- ESP8266 board with 4 MB flash (`d1_mini` in `inim.yaml`).
- RS232 level shifter to the panel's serial port. The firmware uses UART0 swapped: TX on GPIO15 (D8),
  RX on GPIO13 (D7), 57600 baud 8E1. UART0 belongs to the panel, so there are no logs on the serial
  line: read them over the API.
- The author's board is the inside of a WiFi-RS232 converter (D1 mini clone), reflashed with this
  firmware.

## Setup

1. **Secrets**: `cp esphome/secrets.example.yaml esphome/secrets.yaml` and fill it in
   (API key: `openssl rand -base64 32`).
2. **Installation**: describe your panel in `esphome/installation.yaml` (start from
   `installation.example.yaml`): expansions, terminals with zone name and type, areas, scenario codes.
   The scenario codes are read from the panel: activate the scenario from the keypad and copy the
   "Codice scenario" diagnostic sensor.
3. **Network**: adapt `wifi.manual_ip` in `esphome/inim.yaml` (or remove it for DHCP).
4. **Build and flash** (ESPHome in Docker; the first flash over USB, then OTA):

   ```sh
   cd esphome
   ESPHOME="docker run --rm --network host -v $PWD:/config ghcr.io/esphome/esphome:2026.10.0b4"
   $ESPHOME config inim.yaml                          # validate
   $ESPHOME compile inim.yaml
   $ESPHOME upload inim.yaml --device 192.168.1.33    # OTA
   $ESPHOME logs inim.yaml --device 192.168.1.33      # logs over the API
   ```

   Flashing restarts the ESP: the alarm is unavailable in Home Assistant for about 15 s.
5. **Home Assistant**: add the device (ESPHome integration, with the API key), then copy the package
   (`homeassistant/inim.example.yaml`, adapted to your zones and areas) to
   `<config>/packages/inim.yaml` and include it in `configuration.yaml`:

   ```yaml
   homeassistant:
     packages:
       inim: !include packages/inim.yaml
   ```

   Restart Home Assistant (or reload the template entities). A new zone needs its lock in the package
   and a new area its alarm panel.
6. **Dashboard** (optional): paste `dashboard/antifurto-view.example.yaml` into a new view
   ("Edit in YAML"); it needs the toggle helper `input_boolean.mostra_dettagli_guasti`.

**Renaming**: the ESPHome unique id of an entity comes from its name, so rename zones and areas in
Home Assistant, not in `installation.yaml`, or they become new entities.

## How it works

- **States** (zones, areas, faults, memories, event log) are ESPHome entities generated from
  `installation.yaml`. The panel is polled every second (zones, areas, faults) and every 5 s
  (bypass, memories, event log).
- **Commands with the PIN** are API actions (`esphome.inim_rs232_arm`, `arm_area`, `bypass_zone`,
  `include_all`, `reset_memory`). The template alarm panels and locks of the Home Assistant package
  ask for the code and pass it on: the PIN is never stored, and the firmware wipes it once sent.
  ESPHome's own alarm panel and lock aren't used: the panel can't switch between armed modes
  ("Cannot arm when not disarmed") and the lock drops the code.
- The package finds the helper entities by number ("Stato area 0", "Esclusione zona 5"), so they
  don't change when zones and areas are renamed.
- **Transport**: one transaction at a time without blocking `loop()`; stale bytes are drained before
  each request; after a failure the line stays quiet for `settle` and the next reads are done twice
  and compared (the frames carry no request id, so a late answer could pass for the next one).
  Commands queue between polls, are dropped if older than `command_max_age` and retried on failure:
  they set absolute states (arm state, bypass on/off), so retrying is safe.
  The result of every command is on "Last write result".
- If the serial line goes silent, "Connessione centrale" goes off after 30 s and the panels and locks
  become unavailable.

Tunables (in `installation.yaml`, under the component): `timeout` (1 s), `settle` (500 ms),
`poll_interval` (1 s), `slow_poll_interval` (5 s), `offline_after` (30 s), `command_max_age` (20 s),
`confirm_reads` (3), `write_attempts` (2), `log_max_entries_per_poll` (20), `psu_voltage_threshold` (13 V).

## Tests

The protocol helpers are pure C++ and are tested on the host (needs g++; here in a throwaway container):

```sh
docker run --rm -v $PWD:/src:ro -w /src alpine sh -c "apk add -q g++ && \
  g++ -std=c++17 -Wall -Iesphome/components/inim esphome/tests/test_protocol.cpp -o /tmp/t && /tmp/t"
```

## Credits

- [smartliving2mqtt](https://github.com/mvimercati/smartliving2mqtt) bridge this firmware replaces.
- Template alarm panels and locks in Home Assistant, to ask for the PIN: idea from
  [lcavalli/esp-kyo-alarm-generic](https://github.com/lcavalli/esp-kyo-alarm-generic).
