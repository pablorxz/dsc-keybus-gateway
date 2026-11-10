# Zone configuration

The project does not assume that activity proves a zone exists. The default
example in `firmware/zones.default.yaml` selects zones 1–10 for demonstration;
change `visible_zones` to match the installation. A selected zone without an
override appears as `Zona N` in `Sin asignar`, with generic sensor behavior.

Create an ignored local file `.local/physical-test/zones.yaml` to customize a
physical installation, or `.local/bench/zones.yaml` for a disconnected bench:

```yaml
visible_zones: [1, 2, 9]
zones:
  1:
    name: Living room
    group: Main apartment
    type: motion
  9:
    name: Entry door
    group: Main apartment
    type: door
    # Optional: preserve an existing ESPHome/HA entity name during migration.
    entity_name: Existing zone 9 entity name
```

Allowed types are `sensor`, `motion`, `door`, and `window`. Omitted metadata
uses generic values. `entity_name` controls the ESPHome entity name separately
from the dashboard label; without it the entity name defaults to `Zona N`.
Preserve it for existing installations and avoid changing it casually, since
renaming an ESPHome entity may affect Home Assistant identity and automations.
Verify actual Home Assistant migration behavior for the installation. Groups
are display labels, not DSC partitions. Names are not read from the panel.

`python tools/prepare_physical.py` renders the physical configuration using the
optional local file; `python tools/prepare_bench.py` does the same for the bench.
These commands do not contact any device. They overwrite the corresponding local
rendered YAML, so preserve intentional manual edits before running them. Follow
the separate bench or explicitly authorized physical workflow for compilation
and upload. Name changes require rebuilding and reflashing; they are not yet
editable from the dashboard.

The dashboard activity list stores the last 64 observed changes in RAM. It is
cleared at reboot and may miss transitions during disconnects or between
observations. Relative ages do not require a synchronized wall clock. It is not
the panel's event buffer or a durable audit log; use an external recorder for
long-term history.
