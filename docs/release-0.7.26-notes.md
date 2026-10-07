# TAK Engine 0.7.26

0.7.26 follows 0.7.25. It changes how flyers move when they are grouped with
ground units, in both pathfinding modes.

- **Legion: flyers stay with a mixed formation.** In 0.7.25, flyers in an
  Alt-number formation with ground units flew ahead at full speed, landed at the
  destination and waited there, sometimes thousands of pixels in front of the
  army. In Legion games, flyers in a formation that also has ground members now
  hold stations in a tight spiral over the ground units while the formation
  moves, fight-moves or patrols. Near their station they fly no faster than
  the slowest ground member, so they keep pace; a flyer that has fallen
  behind flies at full speed until it catches up. When the ground units
  arrive, the flyers settle over them. In testing, the flyers' largest
  distance from the ground group fell from about 2,000–2,900 px to about
  150 px, and they finish together with the ground instead of about 37
  seconds earlier. Flyers ordered on their own, or to a different
  destination, fly as before. This is a deliberate difference from the
  original game, which never paced flyers. See
  [Legion](legion-pathfinding.md).
- **Retail: grouped flyers follow the original game's rules.** The original
  game runs the same two group checks for flyers as for ground units, and these
  are now ported from the retail binary and checked against it. A flyer that
  is far out of its place around the group's centre is sent back to it. A
  flyer that is found ahead of the centre a second time has a **1-in-4 chance
  to drop all its orders**, as in the original game. The original game never
  slows flyers to the group's speed, so a fast flyer in a mixed group can
  shuttle between the goal and the group, or be left where its orders were
  dropped. This is the original behavior, kept on purpose in Retail mode;
  choose Legion if you want flyers to stay with the army. Only flyers in
  numbered groups or formations are affected; computer opponents issue no
  groups, so AI games play as before. See
  [the pathfinding port notes](pathfinding-port.md).

**Compatibility:** protocol **237**, replay format **11**, generator version
**8**, campaign payload **4** and Crusades SQL schema **9** (all but the
protocol unchanged since 0.7.25). Update clients and servers together; 0.7.25
clients cannot join. Replay playback requires the exact simulation protocol,
so 0.7.25 recordings (protocol 236) need 0.7.25. Generated-map recipes and
saved `.kmp` maps from 0.7.25 remain usable, and saved settings carry over
unchanged.

No new dynamic runtime dependencies or retail assets are included. The README
gallery has new battle, dragon, flyer, naval, interface and generated-map
pictures, and the [user guide](user-guide.md) now has screenshots. See the [release validation report](release-0.7.26-validation.md).
