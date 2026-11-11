# Model rendering: the piece transform

How a unit's 3DO model and its COB piece state become a posed model on screen,
and the evidence for each sign. Render-only: nothing here touches `src/sim/*`
heading semantics, `atan2(dx,dz)` bearings, movement `(sin h, cos h)`, or the
lockstep hash.

## The transform (current specification)

`src/client/modelmath.h` holds the conversions, shared by unit rendering,
shadows, effect anchors and the model viewer (`ModelView`):

- **Script rotations:** `scriptRot` negates all three COB piece angles
  (pitch, yaw, roll) before `Xform::then()` composes `T(offset) · Ry · Rx · Rz`
  down the piece tree.
- **Script translations:** `scriptTransform` negates the COB MOVE X and Z
  components; Y stays.
- **Body pitch and roll:** `modelBodyTransform` negates both and applies them
  before the piece tree.
- **Heading:** every mover, flyers included, faces `−heading`, applied outside
  the piece tree. There is no flyer-specific facing.
- **Emission points:** `modelEmissionPoint` composes the same chain at
  instruction time for weapon/effect emitters (rounded like retail's x87
  conversion, `modelEmissionFixed`); it is a display transform, kept separate
  from integer gameplay point queries.

Why every axis negates: the retail model loader mirrors authored X/Z. Expressed
in our unmirrored authored coordinates, retail's renderer (`0x4ee620` /
`0x4eea20`) conjugates each piece transform, which negates all three script
angles and the X/Z script translations. Native renderer vertex probes (emulating
the composer on shipped models) confirm this specification.

Walking is driven by the unit's own script, as in retail: a Create-started
controller (for example `MoveWatcher`) polls `GET_UNIT_VALUE 29` (horizontal
speed) and starts the `walk` / `walk_legs` / `restore_*` scripts itself; the
engine does not choose a walk script. Unit COB threads and piece motion run in
`World` (see CLAUDE.md).

---

## Evidence

### Models are authored front = −z, right = −x

TAK models are authored **front = −z, right = −x, up = +y** in raw 3DO
coordinates. Proven by piece *data*, which cannot be misread the way rendered
pixels can:

| Model | Piece evidence (raw 3DO, world units) |
|---|---|
| arasword | `FootL/FootR` toes extend to z = **−6.5** (heels +1.8); `Sword` under `HandR` at x = **−10.08**; `Shield` under `HandL` at x = **+10.08**; `WakeL/WakeR` water-wake emitters at z = **+3.0..+11.7** (behind) |
| araking | `Cape1..Cape4` hang at z = **+3.2..+6.3** (a cape is on the back); toes z = **−7.6**; `EmitWpn` at blade tip z = **−41.5** |
| tardemon | `tail1/tail2` extend to z = **+15.2/+11.4**; `lhoof/rhoof` toes z = **−11.0..0** |
| zonhunt | `hair1..hair8` trail at z up to **+16.8**; `winganchor_L/R` + `wing1` attach at z = **+6.5** (wings root on the back); `breast_L/R` at z = **−4.4..+1.9** (front) |

(Reproduce by walking the 3DO tree printing FromParent offsets and vertex
bboxes, e.g. with `modeltool`.)

This matches TA-classic: Spring negates z at 3DO import precisely because raw TA
data is front = −z in a right-handed reading; the TA Design Guild's "front is +z"
describes the authoring tool's display convention, not the bytes. The Zhon
models are authored exactly like every other side's.

### No reflection in the projection

The model→screen basis has determinant −1, and that is correct: BOTH label
systems (world `x=east, y=up, z=south` and model `right=−x, up=+y, front=−z`)
carry one mirror relative to physical space, so the two mirrors cancel
semantically — there is no perceived reflection, and the E/W cards show the
correct body side. No root-level rotation or reflection can fix a
**per-channel sign error in the piece composition**; an X-mirror conjugation
fixes the Ry sign but flips Rz and mirrors static geometry. Sign errors in
model rendering are therefore looked for in the piece channels, never by
mirroring the whole model.

### Root facing `−heading` is retail

KINGDOMS.icd builds the unit matrix with `Ry(−heading)` (`fchs` at `0x4ee6a9`),
and the root matrix call `0x4ee620` is identical for all units — there is no
flyer facing branch. With front = −z, `Ry(−h)` puts face, toes, and lowered
blade toward the movement direction `(sin h, cos h)`.

### Behavioural checks

- **Attack swing** (a signed oracle): arasword `attack1/2` carry large negative
  y-turns (≈ −85°); the windup must rotate the torso/sword toward the **sword
  (−x)** side. Statics alone cannot validate a yaw sign (the restore scripts
  return y to 0).
- **Walkers:** `walk_legs` is entirely x-axis leg swings; with the pitch sign
  right the legs swing forward.
- **Flyers:** the `fly` script pitches the body with large x-axis TURNs on
  hips/torso (≈ 35–57°); with the pitch sign right the body sits upright and
  every flyer flies forward at plain `−heading`. For flyer facing, the rest-pose
  front/back is NOT the airborne front/back: validate against the actual
  `fly`-pose render, not static geometry (`modeltool obj`) and never a single
  by-eye screenshot. A reliable method: tint FRONT pieces red and BACK pieces
  blue, render the flyer moving east, and compare the red and blue centroid x.
- **Helmets:** the gold chainmail **aventail on the BACK of a helmet** is easily
  mistaken for a face; the face side is the dark surface. Judge by piece data,
  not by texture.

---

## Retail transform chain (as evidenced)

Model file → screen. Confidence per element: **[P]**roven / **[S]**trongly
implied / **[A]**ssumption.

1. **3DO load**: vertices/piece offsets ×1/65536; root piece is a 4-vertex
   ground plate, not drawn. The loader mirrors X/Z (see above). [P]
2. **Model space**: front −z, right −x, up +y (left-handed labels, shared with
   TA-classic). [P]
3. **COB piece state**: angles 65536/circle, linear 65536/unit, per-second
   speeds, shortest-path turns, non-blocking `start-script`; state stored in
   script-space signs. [P]
4. **Piece composition**: `world = parent · T(FromParent + move) · R(piece)` down
   the tree, translation before rotation; the sign conversions happen at
   composition time, not in stored script state (composer `0x5ad090`, `fchs`
   `0x4eea98`, axis-field pairing via GetPieceWorldPos `0x4dd104`). [P]
5. **Root**: the same composer with `(rz, −heading, rx)` from unit fields
   `+0x7c/7e/80`; no flyer facing branch (`0x4ee620`, `fchs` `0x4ee6a9`). [P]
   Flyer altitude appears in the blit rather than the matrix. [A — not located]
   FBI `upright`/`pitchscale`/`bankscale` feed the pitch/roll fields. [S]
6. **Projection** (unit-frame v, 16.16): `sx = v.x + v.y/4`,
   `sy = −v.z − v.y/4`, `depth = v.y + 25`; fixed oblique, 1 unit = 1 px, cached
   sprite re-rendered when orientation moves ≥ 8 angle units (vertex loop
   `0x4edb01`; deadband `0x4ee9a0`). [P] Depth-compare direction inside the
   sprite: [A — unresolved].
7. **Blit**: `(unit.x − cam_x + 5, unit.z − cam_z − terrain_height/2)`
   (`0x511170` caller). [P]

## Notes and traps

- **Aim scripts:** bearing/pitch passed into `AimWeapon` use script-space signs
  unchanged; retail negates only at composition.
- **FBI `orientation`** (retail UNIT_DEF `+0x22c`): no shipped FBI sets it.
- **ICD velocity idiom:** one ICD derivation gives a retail velocity
  `(−sin h, −cos h)`, which contradicts the art and the verified cards; one sign
  in that derivation (velocity idiom vs projection vs `fchs` reading) is
  misattributed. The transform above is over-determined by the art and the
  native vertex probes, so it does not depend on resolving it (a close-out would
  re-disassemble around `0x41701c`).
- Never change `src/sim/*` or `src/cob/vm.cpp` to fix a rendering sign: heading
  semantics and script state are retail-consistent and hashed.
