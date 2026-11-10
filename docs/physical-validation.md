# Physical validation and development build

This record follows the isolated milestone. On 2026-10-27 the user explicitly
authorized testing on the installed PC1864 and its identified ESP. Earlier bench
reports are historical and do not include these later observations.

A passive build with GPIO22 clock and GPIO21 data, and no write GPIO, detected
Keybus activity. The user confirmed the physical keypad remained responsive with
no beeping. The panel initially exposed some zone groups as unknown; the first
group became known after receiving valid panel frames. During a user-directed
physical test, zone 9 was observed open and then closed, and zone 1 registered a
motion event. A return to the prior firmware through authenticated OTA succeeded
before the user requested the new code to remain installed.

A full physical build was subsequently loaded on the same identified ESP using
authenticated OTA. It sets GPIO22 as clock and GPIO21 as both read and open-drain
write, uses the native ESPHome API and includes the authenticated local dashboard.
This enables physical transmission: it does not prove commands are accepted by the
panel. The user confirmed the keypad worked normally after restart. The API device
identity matched, the Keybus was connected, partition 1 was observed disarmed and
ready, the dashboard returned HTTP 200 after Digest authentication, and an
unauthenticated state request returned 401. A user-directed second door test
against the full build showed zone 9 open at 13:12:32 and closed at 13:12:47 local time via the dashboard state endpoint.
Real arm/disarm operations were not sent during this initial check; see the
subsequent dashboard cycle below. Zone states may initially remain unknown until
sufficient real frames arrive; unknown must not be interpreted as closed.

`firmware/physical-example.yaml` is a generic template derived from the deployed
layout. The zone list now comes from [zone configuration](zones.md). The active rendered
configuration and its Wi-Fi, API, OTA recovery and dashboard credentials are kept
in the Git-ignored `.local/physical-test/` directory. OTA/API identity should be
checked before a future upload. The previous firmware binary is present locally,
but its availability does not guarantee recovery if network or OTA access fails.

The dashboard uses HTTP Digest but does not encrypt alarm codes in command bodies;
use is limited to a trusted network until a secure transport is implemented. The
physical observations above do not validate long-term operation, all zones, panel
commands, keypad contention or panel interoperability in every state.

## First physical dashboard command cycle

On 2026-10-27 a user-authorized dashboard arm-home operation transitioned from
disarmed through arming to armed-home, with the service reporting confirmed. A
single dashboard disarm attempt was rejected as `panel_rejected`; the configured
legacy access code did not match the master code previously supplied by the user.
No automatic retry was made. The panel subsequently reported disarmed and ready;
the user confirmed they disarmed it at the physical keypad. After the user
confirmed their usual code, a second dashboard cycle armed in stay mode and then
disarmed, each with an observed state transition and a confirmed operation. The
panel ended disarmed and ready. No alarm code is recorded here. At that point,
other modes and operation under interference had not been validated physically.

## Physical dashboard away cycle

On 2026-10-27 the user authorized an away-mode cycle from the dashboard. Before
the test, partition 1 was disarmed and ready, with no trouble and all ten
configured zones reporting closed or at rest. The dashboard submitted one away
command; it observed arming and then armed-away, with the operation confirmed by
the panel. One dashboard disarm command then returned a confirmed disarmed state.
A subsequent snapshot showed the panel connected, partition 1 disarmed and ready,
and no reported trouble. This is a single observed cycle, not proof of long-term
reliability, alarm activation behavior, or operation under keypad contention.

## Physical dashboard night cycle

On 2026-10-27 the user authorized a night-mode cycle from the dashboard. Before
the test, partition 1 was disarmed and ready, with no trouble and all ten
configured zones reporting closed or at rest. One dashboard night command
resulted in arming followed by the observed night/no-entry-delay state, with the
operation confirmed. One dashboard disarm command then returned a confirmed
disarmed state. The subsequent snapshot showed the panel connected, partition 1
disarmed and ready, and no reported trouble. This single cycle does not verify
physical sensor triggering, all programming configurations, long-term behavior,
or operation under keypad contention.

## Dashboard and HTTPS update

On 2026-10-27 the user authorized an encrypted OTA update of the installed ESP
with the redesigned dashboard, local zone metadata, activity list and Digest
replay guard. The existing local HTTPS certificate was reused. The user observed
the physical keypad and confirmed it remained responsive without beeping.
After reboot, authenticated HTTPS returned the dashboard and assets, the panel
reported connected and disarmed, and all ten zone labels matched the local
configuration. The encrypted ESPHome API returned the same 15 entity identities
as before the update. An unauthenticated request and a reused Digest authorization
were rejected; clock activity advanced between two state polls. No physical key,
arm or disarm command was sent. The activity list is volatile and was checked for
availability only. These short checks do not establish long-term reliability,
full replay resistance, browser compatibility, or real sensor event delivery.

## Exit-delay cancellation

On 2026-10-27 the user authorized an encrypted OTA update and a supervised
exit-delay cancellation test. After reboot the user confirmed the physical
keypad was responsive without beeping. The panel initially reported the zone
groups as unknown; all ten configured zones later became known and inactive.
One away-arm request was submitted. After the dashboard API observed exit
delay, one disarm request was submitted. The API subsequently observed the
partition disarmed and reported the disarm operation succeeded. The interrupted
arm operation reported uncertain/superseded, as designed. A final read showed
the panel connected, disarmed, ready, with no reported trouble or fire and no
unknown configured zones. The user also confirmed the physical keypad showed
the system disarmed. No automatic retry was made. This single physical
cycle does not establish behavior for all timing, keypad contention, network
failure or panel configurations.

## Dashboard session code

On 2026-10-27 the user authorized a further OTA update and supervised two
away-arm and exit-delay cancellation cycles. The first cancellation supplied a
code and selected the page's in-memory remember option; the second reused the
code without another dialog. Both cancellations were confirmed by the panel,
and a page reload cleared the remembered code. The final state was connected,
disarmed and ready with no reported trouble or fire; the user confirmed that the
physical keypad remained functional and showed the system disarmed. This is a
limited check of one installation, not a test of every arming mode or failure.

## Arming code continuation

On 2026-10-27 a user-authorized OTA update and supervised physical checks
covered exit-delay cancellation for away, stay and night. Away and stay entered
exit delay without requesting a code. Night requested a code; the dashboard
supplied it to the original operation and the panel entered exit delay. Each
was explicitly cancelled and the panel confirmed disarmed and ready. A further
night exit-delay check used the page's remembered code and was also cancelled.
Afterward the panel reported no trouble or fire and the user confirmed that the
physical keypad was responsive and showed the alarm disarmed. The tests did not
wait for any mode to become fully armed; other panel configurations, Home
Assistant and long-term or concurrent keypad behavior remain unverified.
