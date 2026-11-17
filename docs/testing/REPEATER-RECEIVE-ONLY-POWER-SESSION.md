# Limited Underground MeshTrail Repeater receive-only power session

D-028 / D-098 · Owner present · SenseCAP Solar Node P1-Pro · Three sunless days

| Step | Device | Action | Info |
| --- | --- | --- | --- |
| 1 | Owner and P1-Pro | Confirm consent, exact unit, nonemitting image or invocation, receive evidence, restore route and safe stop criteria. | The smoke image provides inert boot/idle only. Its RX probe has no runtime caller. Defer RX if there is no approved, observable receive path. |
| 2 | Isolated battery logger and known load | Verify calibration, range, sample cadence and burden voltage. Record temperature and pack topology. | Measure battery-terminal voltage and signed current together. Retain pack protections and prevent shunt bypass. |
| 3 | P1-Pro and pack | Disconnect USB and solar. Log off, start and idle at the bench. | Preserve the original export, timestamps and state evidence. Log saturation, restart or voltage cutoff. |
| 4 | P1-Pro and approved receive path | Record stable continuous RX at the 915.000 MHz / 125 kHz reference with no deliberate RF source. | Confirm the effective receive state. Any unverified interval stays unknown. Packet-triggered rearm needs a later stimulus session. |
| 5 | GNSS and P1-Pro | Compare verified acquisition, track, standby and off states if the approved image exposes them. | Use matched conditions. Skip unsupported states and label them unmeasured. |
| 6 | Battery and logger | Capture usable energy to the agreed reserve at representative temperature under a separately cleared discharge plan. | Keep protections intact. Save measured Wh, temperature and the remaining reserve. This step is distinct from a 72-hour service run. |
| 7 | Offline workstation | Map the export to the CSV contract and run the analyzer. Check gaps, state markers, unexpected charge and logger clipping. | A failed input, unexplained negative current, reset or unknown RX state makes that interval inconclusive for battery qualification. |
| 8 | Owner and evidence | Record initial state measurements. Choose temperature, reserve and workload before a later 72-hour zero-solar trial. | The 30/120 forwarding cases are model inputs only. No transmit or intentional RF stimulus in this session. |

Use a normalized UTF-8 CSV with `elapsed_s,battery_v,current_ma,state`.
Use seconds, volts and mA with **positive discharge** and increasing times.
Map state labels from independent observations. For example, use `rx`,
`rx_gnss_track` or `unknown`. Preserve the raw export. Choose the maximum
sample gap before capture from the configured cadence and permitted jitter.
The analyzer integrates signed current and instantaneous V × I, gives
cross-state intervals their own `transition` bucket and flags negative samples.
Minimum voltage and peak current are sampled values. Check clipping, gaps
and state chronology against the original capture before using the totals.

Run these from the repository root with Python 3.10 or later.

```powershell
python -B tools/repeater_power_session.py analyze tests/host/fixtures/repeater_power_sample.csv --max-gap-s 1
python -B tools/repeater_power_session.py model
```

The example CSV is synthetic. For an actual export, replace the path and gap
with those recorded at the bench. The archived T-0055 model assumes continuous
listening, 3.7 V, 0.4 s per forward, 20% load margin, 30% cold derating and 20%
reserve. GNSS on gives 15.50 / 15.80 Wh drawn over 72 h at 30 / 120 forwards
per hour. The corresponding nominal pack estimates are 9,000 / 9,200 mAh.
For a measured pack, compare projected 72-hour energy with 20% margin to
the measured Wh usable at the agreed temperature and reserve. Apply each
derating once. A real service trial must confirm availability and reserve.

[US915 review and owner decisions](REPEATER-US915-SETTINGS-PROPOSAL.md)
