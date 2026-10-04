# Kingdoms official-archive verification

Investigation: 2026-10-02. Read-only analysis of locally owned retail files,
under [the repository RE rules](../../CLAUDE.md) and
[retail-engine notes](../retail-engine.md). No retail executable was launched,
patched, or committed; no verification setting was changed. Only documentation
and an independent, read-only research verifier were added. Engine behavior is
unchanged.

## Findings and confidence

**Follow-up result:** after the initial verification-only investigation, the user
explicitly requested private-key derivation. Recovery succeeded from weaknesses
in existing archive signatures (nonce reuse and a very small nonce), not by a
general discrete-log search. The recovered exponent reproduces the public key
exactly. See the recovery section below. Private material is stored outside the
repository and is not included in this document or the verifier.

**Confirmed by signatures from 16 original archives:** `KINGDOMS.KEY` contains a
finite-field **public verification key**, not a private signing key. The archive
signature verifies a custom 24-byte checksum summary of selected header fields
and the **stored directory block**. It does **not** directly sign every archive
byte. The equation is Nyberg–Rueppel-style, with a 1024-bit modulus and a 164-bit
subgroup order. There is no SHA/MD5 invocation in this authentication path.
The equation below, rather than the algorithm-family name, is the precise result.
No particular cryptographic library/version has been conclusively identified.

**High confidence, static call-path evidence:** normal root HPI/UFO mounting
rejects failed authentication when the key is configured. Missing/unavailable
key handling clears the key configuration and skips this authentication filter;
format, footer, and compression checks remain separate. This is an observation
of retail behavior, not a recommended engine policy or a runtime bypass test.

**Confirmed across readable builds:** the checksum calculation agrees in base
2.0, Iron Plague CD 3.0, official patched 3.0, and GOG 3.0. Corresponding directory
payload construction and signature-verification routines have the same layout
and operations. The 1.0-era ICD was not resolved; claims here do not establish
unpatched launch-version behavior.

## Evidence identity

Versions below are PE file versions, not filenames or marketing labels. The
local `ironplague.icd` actually reports 2.0.0.1. Provenance of official patches
and the GOG packaging is recorded in
[Darien Crusades sources](darien-crusades/sources.md) and
[distribution provenance](darien-crusades/distribution-provenance.md).

| Evidence | PE version | Bytes | SHA-256 |
|---|---|---:|---|
| `assets/game/KINGDOMS.icd` (GOG) | 3.0.0.1 | 2277500 | `1144a394889811ae6d113f8fe470dac5de0b0a7aae3c9ee063f74b8d20b730db` |
| Official patch, `standard/MAINDIR/KINGDOMS.icd` (also `crusades/MAINDIR`) | 3.0.0.1 | 2277500 | `6ddc7fae0cbf3ee13d530614b64608a3aeefca7deb73719310a4f68fe1f22e96` |
| Iron Plague CD, `iron-plague-cd/Kingdoms.icd` | 3.0.0.1 | 2244725 | `60a65c1ef99eb360929591873463af4e87df270ff10039954db8e0014714b014` |
| Official `TAK1x-20.EXE/MAINDIR/KINGDOMS.icd` | 2.0.0.1 | 2216051 | `db8d581ad96700491c11e9baa7c1023fb9d1fa3bb0efe5e03fafc651454c3112` |
| Official `tak1x-bone.exe/MAINDIR/KINGDOMS.icd` | 2.0.0.1 | 2220154 | `c408049635bce3cb8a1c2d4327d47c51e91485effa9702e369b401bc36b31cd0` |
| `assets/game/ironplague.icd` (GOG) | 2.0.0.1 | 2220154 | `cb3b33c2072839b248048e2f8b472632bbc0084e83357e53756ce909d2f5a7da` |
| Official `tak30BAupdate.EXE/MAINDIR/KINGDOMS.icd` | 3.0.0.1 | 2277500 | `bdca0a7fda2c50b20a112d256ff607270baa0eaadda8cd02c1c1b818d14acf59` |
| `v1.1BAPatch.exe/MAINDIR/KINGDOMS.icd`, unresolved | 1.0.0.1 | 2281565 | `dc5a53749c5a9bfcf9e1ecfc2cc4c483ee2a62036af360fbb02a9960f8e15ab2` |
| `assets/game/Kingdoms.exe`, launcher | — | 192512 | `74f0fe44c0473b875a6b14ef3ddc2e109d31210821c2c765b29ddc95b34eec8e` |

Paths without an `assets/game` prefix are beneath the local
`assets/research/darien-crusades` collection (patch extractions under
`ftp-survey/extracted`). These are local evidence locations, not tracked assets.
The small launcher has neither key-name nor verification-switch literal; the
archive loader and arithmetic traced here reside in the ICD. Its imports include
process creation, not a Windows cryptography API. Absence of those imports alone
would not identify an algorithm; the ICD arithmetic and archive results do.

## Key encoding

The inspected `Kingdoms.key` is 422 bytes, SHA-256
`05ce964fe2763b83a3d9e55af81d3d4c4fbb0a4214b390da021bc9bc5ec34db2`.
It is one DER SEQUENCE containing exactly four positive ASN.1 INTEGERs:

| Field | TLV file offset | TLV header bytes | Value bytes | Significant bits |
|---|---:|---:|---:|---:|
| Sequence | `0x000` | 4 | 418 | — |
| `p`, field modulus | `0x004` | 3 | 129 | 1024 |
| `q`, subgroup order | `0x088` | 2 | 21 | 164 |
| `g`, generator | `0x09f` | 3 | 128 | 1022 |
| `y`, public element | `0x122` | 3 | 129 | 1024 |

The 129-byte integers include a DER sign-padding zero. Integers are big-endian.
There is no algorithm OID, SPKI wrapper, checksum table, or private exponent.
The constructor consumes fields in this order. The observed parameters satisfy
`q | (p-1)`, `g^q mod p = 1`, and `y^q mod p = 1`. These checks are not a primality
proof. Public `y` is not the private exponent needed to sign content.

## Archive structure and exact coverage

The loader explicitly checks Kingdoms **HPI v2**, not original TA HPI v1. Its
32-byte header comprises eight little-endian uint32 fields:

| Offset | Field | Included in authentication summary? |
|---:|---|---|
| 0 | `HAPI` magic | No; separately checked |
| 4 | `0x00020000` version | No; separately checked |
| 8 | Stored directory offset | Yes |
| 12 | Stored directory byte count | Yes |
| 16 | Stored filename-block offset | Yes |
| 20 | Stored filename-block byte count | Yes |
| 24 | Data start (32 in tested originals) | Yes |
| 28 | Signature offset | No; used to locate the signature |

Precisely, with half-open byte ranges:

```text
P = archive[8:28] || archive[directory_offset:directory_offset + directory_size]
```

This is the directory block **as stored**, including its SQSH header and
compressed bytes when compressed. It is not the decompressed directory tree.
Filename-block bytes and file payload bytes are not inputs to this signature.
Their offsets/sizes, and the per-file metadata inside the directory, are covered.

At the header's signature offset are two fixed-width big-endian integers:
`r[21] || s[21]`. There is no ASN.1 signature wrapper. Width is computed as
`ceil(bit_length(q)/8)` from the key, so the observed signature is 42 bytes.
All 16 tested originals place a 36-byte ASCII copyright footer immediately after
it, ending the archive: `Copyright 1999 Cavedog Entertainment` or the 2000
variant. No trailing NUL is stored. Retail independently checks the final 36 bytes,
replacing the four year positions with `0000` before comparing. Those four
positions therefore do not establish a checked numeric year. Neither the footer
nor the signature-offset header word participates in `P`.

### Checksum summary and verification equation

For bytes `v_i` of `P`, with indices starting at zero, compute four uint8 lanes:

```text
a = sum(v_i)                           modulo 256
b = XOR(v_i)
c = sum(v_i XOR (i modulo 256))        modulo 256
d = XOR((v_i + i) modulo 256)
B = a | (b << 8) | (c << 16) | (d << 24)
```

Read `n = floor(len(P)/4)` little-endian uint32 words `W_i`. For each word use
`j = n - 1 - i`, and compute:

```text
S0 = sum(W_i)                         modulo 2^32
S1 = XOR(W_i)
S2 = sum(W_i XOR j)                   modulo 2^32
S3 = XOR((W_i + j) modulo 2^32)
```

Trailing 1–3 bytes affect the byte lanes but not the word lanes. Serialize
`[len(P), B, S0, S1, S2, S3]` as **six little-endian uint32s**, giving 24 bytes.
Interpret those 24 bytes as one **big-endian integer** `m`. This mixed-endian
step is required to reproduce the retail result.

The signature is accepted when:

```text
r != 0
r == ((g^s * y^r mod p) + m) mod q
```

Exponentiations and products are modular integer arithmetic. This is a
Nyberg–Rueppel-style verification equation, not DSA's verification equation.
There is no SHA-1, SHA-256, MD5, or CRC polynomial in this summary calculation.
The custom summary is not a modern collision-resistant hash. No collision,
forged signature, or modified working archive was constructed in this research.
SHA-256 values in this document and the tool output are our evidence identities,
not retail authentication fields.

## Loading policy and other integrity mechanisms

The root mount routine initializes the key configuration, enumerates HPI and UFO
archives, and calls the archive factory. Opening an archive checks format and
copyright, constructs `P`, obtains the signature width from the key, reads the
signature, and records the verification result in archive object field `+0x38`.
It then loads/decompresses the metadata. **Opening and authenticating are distinct:**
a signature failure alone does not immediately fail the low-level open routine.
The root mount caller queries the authentication flag and removes failed archives
when a key filename is active.

| Condition | Observed policy / confidence |
|---|---|
| Valid key, valid original signature | Authenticated; original corpus verified independently. High confidence. |
| Valid key, invalid/absent signature | Authentication cannot succeed; root mount filter removes an otherwise opened archive. High confidence from static path; no live UI test. A truncated file can fail earlier. |
| Missing/unreadable key | Bootstrap's file check returns zero, clears configured key filename, and root mounting skips its authentication filter. High confidence for this path; required-data availability may still prevent startup. |
| Malformed nonempty key | Availability check is only a file checksum, not DER validation. Later decoding can fail; exact exception/UI behavior remains unresolved. |
| Changed signed directory/header data | Covered input changes. In-memory one-bit negative checks failed verification. Not a claim that this weak summary detects every possible modification. |
| Changed names or file data outside `P` | Outside this direct signature's coverage. Compression/format checks may fail separately; no claim of whole-file authenticity. |

`disablecavedogverification` has two relevant paths: a plain command-line parser
sets a global flag, while earlier key bootstrap independently recognizes an
obfuscated spelling assembled on the stack and skips normal key setup. Tracing
only the printable literal would miss that bootstrap. A later monitor also
observes key configuration changes. These are static findings; the switch was
not exercised, and no bypass instructions or engine integration were added.

Other checks must not be conflated with official authentication:

- A v2 file entry is 24 bytes: name offset, file offset, uncompressed length,
  compressed length, timestamp, and a checksum at entry `+20`. The archive writer
  computes that last field from the **uncompressed file** using the four byte
  lanes above. Thus the directory signature covers stored file-checksum values
  indirectly. The normal file-read routine examined does **not** compare that
  field; it reads the file and optionally decompresses it. Other consumers of
  that field have not been exhaustively excluded.
- SQSH has a 19-byte header: magic at 0, version byte at 4, method at 5,
  obfuscation flag at 6, compressed length at 7, uncompressed length at 11, and
  checksum at 15. Its checksum is the uint32 **sum of stored compressed bytes**,
  checked before optional deobfuscation. This is not a signature or a keyed MAC.
- Optional SQSH deobfuscation transforms byte `v` at index `i` into
  `((v - i) modulo 256) XOR (i modulo 256)`. It is byte obfuscation, not public-key
  encryption. The decompressor dispatches method 1 to an LZ routine and method 2
  to the zlib/DEFLATE path and checks resulting size. This analysis does not
  generalize that behavior to original TA's v1 archive encryption/layout.
- The archive object also stores a four-byte checksum of `P` at `+0x40`, exposed
  separately from the authentication boolean. The key availability helper uses
  the same four-byte checksum family. Neither is the 42-byte signature.

## Address map

Addresses are virtual addresses in the GOG 3.0.0.1 ICD. Image base is `0x400000`;
for all code/data locations below the PE mapping gives
`file offset = VA - 0x400000`. Relevant bootstrap, archive-open, payload-builder,
key/signature-wrapper and verifier regions are byte-identical in the official
3.0 patch listed above.

| Purpose | VA | File offset |
|---|---:|---:|
| Key-name literal | `0x60e5b0` | `0x20e5b0` |
| Pointer to key-name literal | `0x60e574` | `0x20e574` |
| Printable switch literal | `0x61a308` | `0x21a308` |
| Plain switch parser push instruction | `0x5337bd` | `0x1337bd` |
| Key bootstrap / independent switch recognition | `0x48cbb0` | `0x08cbb0` |
| Root mount and authentication filter | `0x48cd10` | `0x08cd10` |
| Set / get configured key filename | `0x53bf50` / `0x53bfd0` | `0x13bf50` / `0x13bfd0` |
| Key-file availability/checksum | `0x53bff0` / `0x54a770` | `0x13bff0` / `0x14a770` |
| Archive factory / registered mount | `0x53cc20` / `0x53cd80` | `0x13cc20` / `0x13cd80` |
| Archive constructor / open | `0x539400` / `0x539470` | `0x139400` / `0x139470` |
| HPI v2 magic/version check | `0x53ca20` | `0x13ca20` |
| Exact `P` construction | `0x53c100` | `0x13c100` |
| Signature width from key | `0x54af00` | `0x14af00` |
| Signature wrapper / summary calculation | `0x54b2c0` | `0x14b2c0` |
| Isolated summary instruction range (end exclusive) | `0x54b373`–`0x54b432` | `0x14b373`–`0x14b432` |
| Four-field DER key constructor | `0x54eb20` | `0x14eb20` |
| Signature equation verification | `0x54ed90` | `0x14ed90` |
| Big-endian integer input | `0x55af60` | `0x15af60` |
| Joint modular exponentiation | `0x556e70` | `0x156e70` |
| Big-integer addition / remainder / comparison | `0x55ddb0` / `0x55f200` / `0x55f8f0` | `0x15ddb0` / `0x15f200` / `0x15f8f0` |
| Authentication flag getter | `0x53e020` | `0x13e020` |
| Separate archive checksum getter | `0x53e030` | `0x13e030` |
| Normal file read | `0x539c80` | `0x139c80` |
| Writer stores uncompressed-file checksum | `0x53c8dc`–`0x53c8e1` | `0x13c8dc`–`0x13c8e1` |
| Four-lane byte checksum wrapper / calculation | `0x540390` / `0x54a6f0` | `0x140390` / `0x14a6f0` |
| SQSH decode / byte sum / deobfuscation | `0x5496d0` / `0x5498e0` / `0x5498b0` | `0x1496d0` / `0x1498e0` / `0x1498b0` |

The generic whole-file signature helper at `0x53c010` (file `0x13c010`) is a
different caller of the same signature primitive. It treats a trailing signature
as separate from the rest of a file. **Do not use its whole-file coverage to
infer HPI coverage**; archives use `0x53c100` instead.

Equivalent functions in other readable versions:

| Build | Archive open VA | `P` builder VA | Summary byte loop VA | Signature equation VA |
|---|---:|---:|---:|---:|
| Official/GOG 3.0 | `0x539470` | `0x53c100` | `0x54b395` | `0x54ed90` |
| Iron Plague CD 3.0 | `0x53fdd0` | `0x542a60` | `0x551d25` | `0x555750` |
| Official base 2.0 | `0x52fd80` | `0x532940` | `0x541db5` | `0x545880` |
| Boneyards/GOG 2.0 | `0x52c220` | `0x52ede0` | `0x53e085` | `0x541a90` |
| Alternate 3.0 BA update | `0x539420` | `0x53c0b0` | `0x54b345` | `0x54ed40` |

File offsets for these code locations likewise subtract `0x400000`.

## Reproduction and validation

The standalone [verifier](../../tools/re/verify_official_hpi.py) needs Python 3.11+
and the user's own files. It reads only, never extracts/decompresses archive
payloads, and includes no embedded retail key or signing implementation:

```sh
python3 tools/re/verify_official_hpi.py --key /path/to/Kingdoms.key /path/to/*.hpi
python3 tools/re/verify_official_hpi_test.py
TAK_AUTH_RETAIL_DIR=/path/to/retail python3 tools/re/verify_official_hpi_test.py
```

Output is JSON Lines. `directory_signature_valid` is deliberately separate from
`copyright_footer_valid`, and `file_payloads_checked` is always false. Exit 0
means every supplied archive passed the directory-signature and footer checks;
1 means an archive failed; 2 is an argument/key error. It is a research verifier,
not a complete archive validator or an emulation of all retail error behavior.
It uses stricter DER/bounds checks, a 4 KiB key limit, 4096/512-bit arithmetic
limits and a 64 MiB stored-directory limit. These are tool safeguards, not
recovered retail restrictions. Full-file SHA-256 identification is streamed.

Completed checks:

- All 16 installed original HPI archives passed signature and footer verification.
- All six HPI archives directly present in the local Iron Plague CD extraction
  also passed with the same key.
- Six automated tests passed, including optional corpus tests over all 16 local
  originals. For each archive, detached in-memory changes to a signed header
  byte, first/last stored directory bytes, and signature byte were rejected.
  No changed archive was written or mounted. Asset-free tests cover DER errors,
  bounded archive reads, zero/short signatures, and checksum observations.
- Isolated x86 checksum instructions were emulated using Unicorn in a temporary
  harness outside the repository. In **four builds**, all **29 inputs** matched
  the independent implementation: 16 real archive summaries plus deterministic
  synthetic lengths 0, 1, 2, 3, 4, 5, 19, 20, 21, 255, 256, 257 and 4096. This
  checks tail handling, index wrap and word order. It did not emulate a whole
  game or replace an authentication result.
- Byte comparisons confirmed identical official-patch/GOG regions:
  `[0x48cbb0,0x48cf00)`, `[0x539400,0x539800)`, `[0x53bf50,0x53c200)`,
  `[0x54af00,0x54af90)`, `[0x54b2c0,0x54b480)`, `[0x54eb20,0x54eec0)`.

### Verified installed corpus

All rows below passed. Hashes identify evidence, without redistributing assets.

| Archive | Bytes | SHA-256 |
|---|---:|---|
| `IPData.hpi` | 51012805 | `4c7fbe0a71c6ed8c4169681a5690e23f6129fe02ca663fa978acb3aa901a48d4` |
| `IPEnglish.hpi` | 10543045 | `5c7242d783cace7f1ad6ad45fdc3323f9f0a9d2aa45903fb2df23de43331f168` |
| `IPMissions.hpi` | 2495231 | `094f3c143ea4a7dddef4ee3f7606c2670075874a24b74fdd993c3f8954d2615f` |
| `IPSections.hpi` | 6762958 | `82c5556c06cb26a74bad41e432f771ad05bbef423f47c7356d44c7b60e8b7e47` |
| `Jersey.hpi` | 67469 | `9a6f15b2537cf40219050f01427ff13b340925d820787a65c8bb26d86f87e37e` |
| `V2Rocket.hpi` | 486289 | `c8e1d3f0e40cb1b3f1b22a8b2f9088dd20f70af8771fa512aa67de4319639fca` |
| `V3Rocket.hpi` | 2735599 | `2c071a66e2388a9c93ea859b6c635b8508d6d2c59e7b85815cd4a2261e3bc7a7` |
| `boneyards.hpi` | 460225 | `dfd87d26cc669b44ad39938c1637ac6af5e7447f550cc192841a9299a8eecc77` |
| `boneyards2.hpi` | 48535 | `0c15b908c4c5a73a6c5e2ea0805d6116910041957c46d02b729d26f1f94d0e76` |
| `data.hpi` | 51735874 | `3a489d9169af350db2153262421f503b9dfb89fded260e30e7e06eb72427c0d6` |
| `english.hpi` | 5124645 | `d6812403790948981f034a03e3c25a5d8b0789a8ffda0bdb68e705c81787c6b5` |
| `maps.hpi` | 3329064 | `0ca79f9be67afe84d3750a5705741f01ab3b6ad967bbed0b2cb8595097d17b31` |
| `meta.hpi` | 2105720 | `fe6d90d3603b854759a492aa07f0f3c93664cc6e90507c0b7404aedce5d8a147` |
| `missions.hpi` | 4894384 | `a8423768a8a832360b1ca420ccb30634a717e335ac3d6d560c94ac4d19e2ab0b` |
| `sections.hpi` | 28582128 | `a55561b6572c3f42f6da84d3aaed4256398eca7b2a593e1acffc8485aa175b4f` |
| `terrain.hpi` | 132242429 | `9486cfd0df19d356099170ad01f84b9c990c0e9c0683906982b2bcb34ad229eb` |

### Verified CD extraction corpus

| Archive | Bytes | SHA-256 |
|---|---:|---|
| `data.hpi` | 51735874 | `3a489d9169af350db2153262421f503b9dfb89fded260e30e7e06eb72427c0d6` |
| `english.hpi` | 5124645 | `d6812403790948981f034a03e3c25a5d8b0789a8ffda0bdb68e705c81787c6b5` |
| `maps.hpi` | 3329064 | `0ca79f9be67afe84d3750a5705741f01ab3b6ad967bbed0b2cb8595097d17b31` |
| `missions.hpi` | 4894384 | `a8423768a8a832360b1ca420ccb30634a717e335ac3d6d560c94ac4d19e2ab0b` |
| `sections.hpi` | 28582128 | `a55561b6572c3f42f6da84d3aaed4256398eca7b2a593e1acffc8485aa175b4f` |
| `terrain.hpi` | 132242429 | `9486cfd0df19d356099170ad01f84b9c990c0e9c0683906982b2bcb34ad229eb` |

## Remaining limits

No live missing-key, unsigned-archive, or modified-archive game session was run;
those policy findings are based on the traced loader and its callers. No exact
error-dialog text or startup outcome with missing required content is claimed.
Malformed-key exception handling and every alternate archive-mount caller remain
unresolved. The ordinary file-read path does not establish whether a separate
asset-specific caller ever checks the directory's per-file checksum.

The v1.1 patch's version-1.0.0.1 ICD did not expose matching literals or routine
patterns in this analysis. Protection/encoding is a possibility, not a proven
explanation; it was not bypassed. Accordingly, “base versus expansion” here is
confirmed for readable patched base 2.0 versus Iron Plague 3.0, not every original
release or locale. Full modular arithmetic was independently verified against
original signatures but was not dynamically emulated end-to-end in each build.
The initial verification-only investigation did not recover a private key;
the subsequent explicitly requested recovery is documented below.

## Follow-up: shipped private-key material search

On 2026-10-02, a read-only scan examined 15,085 distinct local files totaling
4,025,370,911 bytes beneath `assets/game`, `assets/research/darien-crusades`, and
`assets/extracted/all`. Files sharing a filesystem inode were scanned once.
There were no reported read errors. No retail file was changed.

The scan looked for key-container filename extensions (`.key`, `.pem`, `.der`,
`.pfx`, `.p12`, `.pvk`, `.prv`), private/signing/secret-key names, common private
PEM labels, and recognizable encodings of the known public parameters: the
1024-bit modulus in big-endian, little-endian and lowercase hexadecimal form,
and the complete public DER key in binary or uninterrupted Base64 form.
The only filename candidate and only content matches were the already inspected
422-byte `assets/game/Kingdoms.key`. No private key was recovered.

Static inspection also found that the signing-related archive helper at
VA `0x53ca60` (file offset `0x13ca60`) passes a caller-supplied key argument to
`0x54b020` (file `0x14b020`, call site VA `0x53cab6`). That wrapper passes the
argument to its input constructor at `0x5507c0` and then a key decoder at
`0x54f530`. This is evidence of key-input plumbing, not evidence of an embedded
private key. Neither helper was executed. A direct relative-call scan found no
caller of `0x53ca60`; this does not exclude indirect calls.

This is a bounded recognizable-material search, **not proof of absence**.
Compressed/encrypted installer members not already extracted, unfamiliar
encodings, and a standalone unlabelled private exponent would not necessarily
match. The scan did not attempt exhaustive exponent search, cryptanalytic key
recovery, signature forgery, or any change to verification behavior. Search code
and detailed local scan output stayed in temporary storage; no key bytes were
added to the repository.

## Follow-up: private-exponent recovery confirmed

The user subsequently requested derivation, extending the earlier read-only
inspection scope to mathematical analysis of existing signatures. No game was
launched, no verification decision was overridden, and no archive was signed or
modified.

A scan of 115 local HPI/UFO files yielded 96 instances with signatures validating
under the known key, representing 29 distinct `(m,r,s)` records. Nineteen other
files were skipped for nonmatching format, bounds, or invalid signatures. There
were no read errors. Both `p` and `q` passed 64-round Miller–Rabin probable-prime
checks, and the subgroup checks passed. These are probable-prime tests, not
formal primality certificates.

The signature records contained repeated signing commitments among `Jersey.hpi`,
`V2Rocket.hpi`, and `ASiege.ufo`. Separately, a search of positive nonces through
1,000,000 found a very small nonce in a `boneyards2.hpi` signature. Each of the
three repeated-commitment pair calculations and the small-nonce calculation
produced the **same private exponent**. The three pairs share records and should
not be mistaken for three statistically independent experiments. The small-nonce
calculation provides a separate recovery route.

Validation:

- The recovered exponent is in `(0,q)` and satisfies `g^x mod p == y` exactly.
  This establishes that it is a signing exponent corresponding to the inspected
  public key; it is not merely a candidate inferred from a checksum collision.
- For all 29 distinct valid signature records, the nonce implied by the recovered
  exponent satisfies the observed signature relation.
- Searching private exponents within 1,000,000 of zero or `q` found no match;
  recovery depended on signing-nonce weaknesses, not a tiny private exponent.
- No new signature was generated as a test. Existing signatures and the public
  key were sufficient to validate the result.

This overturns the earlier practical expectation that private-key derivation
would require a general cryptanalytic search. The public DER file still contains
only public parameters: the recovery used weaknesses in shipped signatures,
not an extra secret field in `KINGDOMS.KEY` or a shipped private-key container.
It does not establish why the original signing process generated those nonces.

The derived parameters were saved in a newly created directory outside the Git
checkout with mode `0700`; its JSON key file has mode `0600`. Neither the private
exponent nor the recovery harness is included in the repository. The research
verifier remains read-only and contains no signing capability. Retail files and
engine code remain unchanged.
