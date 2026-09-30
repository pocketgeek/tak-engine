# Campaign rules recovered from shipped help

The patch payloads contain three relevant Cavedog help pages: HELP-144,
HELP-146 and HELP-147 in [sources.md](sources.md). This is primary documentation
shipped with the patches, not a later account of Total Annihilation's Galactic
War. Original HTML remains outside Git.

**Version limitation:** Help144 explicitly describes the December 8, 1999 open
beta. Help147 discusses the upcoming Iron Plague expansion and features not yet
released. Shipping those pages in the 3.0 patches proves their inclusion, not
that every described rule survived unchanged. CONFIRMED below means the
specified documentation claim; exact final-release behavior remains subject to
binary or server evidence.

## Territory capture

| Documented rule | Primary location | What remains unknown |
|---|---|---|
| Battles take place on contested territories; captures can make neighboring regions contested | FAQ 4; Quickstart territory/gathering sections | Exact eligibility and neighbor transition algorithm; graph data |
| Resistance differs by territory and attacking allegiance | FAQ 5a | Per-territory values, modifiers and threshold comparisons |
| Momentum uses the latest twenty battles and updates as reports arrive | FAQ 5b | Server window maintenance, ordering, treatment of disputed reports and capture threshold |
| Neighbor ownership affects how readily territory can fall | FAQ 5c; Help144 game-rules section | Edge weights, influence formula and propagation order |
| Time contested erodes resistance | FAQ 5d | Update interval, rate, bounds and rounding |
| Population/traffic affects campaign behavior | FAQ 5e | Measurement period and scaling formula |
| Boneyards servers perform the campaign calculations | FAQ 5 closing paragraph | Precise wire messages, authoritative storage and client/server call division |

The client corroborates a momentum-history representation: it counts H and T
characters in a supplied `momentum` string and prepares side counts for the
recon display. It does not truncate that string to twenty entries in the traced
loop. The twenty-entry policy comes from the FAQ, not this display routine.
See [binary notes](binary-notes.md).

README-3 additionally documents captures without a battle when fatigue and
side support pass toughness. That is compatible with time erosion, but it does
not supply enough information to collapse all of these factors into one exact
historical equation. The plan's sample formulas remain illustrative only.

## Battle results and eligibility

- FAQ 8 says player rank affects the victory points at stake. It gives no rank
  multiplier table or complete win/loss formula.
- FAQ 11 describes **orphan battles**: a battle finishing after its territory
  has been captured still has its result recorded. If the territory becomes
  contested again, that result contributes to the appropriate side as an
  entrenchment bonus. This documents deferred results, not a rule to discard
  them. Persistence duration, deduplication and cross-crusade behavior are not
  specified.
- FAQ 13–14 documents two- and four-player battles, with two players able to
  team up for one allegiance. Do not infer an arbitrary modern team-size limit
  from the engine's general multiplayer capabilities.
- FAQ 15 and 20 describes Honor normally using Aramon/Zhon/Veruna and Terror
  using Taros/Zhon/Veruna. The same source allows exceptional alliances; these
  lists are not evidence for unconditional hardcoded race bans.
- FAQ 10 makes Crusades tuning part of campaign battles, while describing it
  as an option outside the campaign. The exact unit-value audit is separate.
- FAQ 19 describes an allegiance switch costing one rank and requiring house
  allegiance compatibility. Rank floors and any cooldown remain unknown.

The shipped client's battle-entry failure callback can display a same-allegiance
rejection and an allegiance-capacity rejection. That corroborates restrictions
being communicated to the client; it does not by itself recover server checks
or result submission. Disconnects, draws and invalid reports remain UNKNOWN.

## Campaign lifecycle and presentation

FAQ 22 and 30 describes a war ending through substantial territorial control
plus designated victory locations, rather than requiring every territory.
Objectives vary between wars; the overview presents current requirements.
A completed war offers a history movie, followed by a new war after a reset.
The FAQ mentions enemy capitals as usual objectives, not universal constants.

Quickstart distinguishes map selection, territory recon, overview and history.
Recon supplies the associated battle map, restrictions, description and status.
Campaign gatherings correspond to contested territories; players do not create
arbitrary gatherings. Both Quickstart and the FAQ describe houses as unfinished,
which must not be confused with proof that the shipped client lacks house code.

## Consequences for the reconstruction

The modern model will need room for separate territory ownership and contested
state, side-specific resistance, configurable victory locations, recent-result
history and deferred results. These are requirements suggested by primary
sources, **not new implemented behavior**. No numeric fatigue, momentum,
influence or rank formula is justified yet.

The remaining historical work is narrower now: recover authoritative territory
parameters/edges and result messages; establish later-version differences;
complete the exact Crusades Balance audit. No server implementation is claimed
complete by this document.
