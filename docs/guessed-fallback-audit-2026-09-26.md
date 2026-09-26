# Guessed-behavior corrections — 2026-09-26

This pass addresses the five concrete guesses identified after the death-audio
correction. It does not remove ordinary missing-resource handling or claim that
every remaining engine behavior has been proven against retail.

| Area | Correction | Evidence and limits |
| --- | --- | --- |
| Firing audio | Remove generic weapon-kind clips; retain authored callbacks and silence. | [Roster and native dispatch checks](guessed-firing-audio-2026-09-26.md). Three referenced archer clips are absent from the installed assets. |
| Impact effects | Missing authored classes produce no generated substitute. Water classes apply to environmental water contacts, not direct unit hits. | [Native impact dispatch and variants](guessed-impact-effects-2026-09-26.md). |
| Airborne effect placement | Carry actual contact XYZ and captured per-slot muzzle/aim geometry; remove nearby-aircraft height guesses. | [Placement implementation and regression checks](guessed-impact-effects-2026-09-26.md). Status-weapon delivery timing remains a separate limitation. |
| Area clearing | Keep an authoritative area order and reselect after each target from the builder's current position. | [Native selector comparisons and queue tests](guessed-area-clear-2026-09-26.md). The project's prohibition on targeting unseen objects remains. |
| Legacy mission runner | Remove the duplicate numeric MAP_COMMAND interpreter; route Debug's `--mission` alias through the existing campaign path. | [Mission cleanup](guessed-mission-path-2026-09-26.md). This does not assert complete campaign parity. |

Retail comparisons used static inspection and instruction emulation of the
installed binary. No retail game GUI was launched. Existing pathfinding and
individual reclaim movement controllers are unchanged.

Area clearing changes the authoritative command format and bumps the protocol to
**182**. Client and server must be updated together. The existing version gate
rejects older peers and replays; ordinary commands retain their prior layout.

## Integration validation

All targets rebuilt in Release, optimized Debug and regular Debug. All **186**
CTest runs passed (60/63/63), including the new firing roster and persistent-area
regressions. The impact fixture additionally passed with Crusades balance.
The mission alias and canonical campaign launch produced matching headless
multiplayer state hashes (`a5032e7b986d7a1c`).

GCC/Clang determinism checks agreed on golden `dcef618cd2e4d558`. ARM cross-build
checks were skipped by the harness because their target build dependencies were
unavailable; this pass does not claim fresh ARM execution coverage.

Two fresh local multiplayer runs both retained the prior state hash
`3c4e5e85a939988c`. The 16,000-unit, 900-tick patrol comparisons matched all 30
pre-change checkpoints in each balance mode: final standard
`211f4c5ea4ff4cd9`, Crusades `af3cee7abc7bebbd`. These checks support preservation
of existing movement behavior; they do not substitute for the new area's focused
selection and command tests.
