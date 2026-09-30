# What kingdoms.mmz contains

Classification: CONFIRMED container/content observations; updater role is
RECONSTRUCTED from its declarations and the companion DLL's exported API.
Provenance: the two fingerprinted patch payloads in [sources.md](sources.md).

Each `kingdoms.mmz` is a ZIP containing one `Kingdoms.mmm` member. Neither
manifest is a persistent campaign database or a territory rule script.

| Payload | Decoded bytes | Decoded SHA-256 | Package version declaration |
|---|---:|---|---|
| Standard | 19475 | `8f1c55dbbc62cf38909f2b03ee0acf28b4955497a87bb05b4325bd94bcd15aa6` | 30BA |
| Crusades | 19884 | `3bf5a7c7f88849bc9d2d459cf6ba917c7b02c3ff58beb48f8f534f7409e1614b` | 30BB |

The decoded format identifies Maia version 2.0 and declares Win32 package
metadata, HTTP/FTP update locators, bundles, resource paths, byte sizes, MD5
values and bootstrap version relationships. The companion `bymaia.dll` exports
package update, source URI, package version, local verification and restart
configuration APIs. Export presence does not prove a specific call was used.

The differences include a 30BB bootstrap bundle, changed advertised hashes and
sizes for selected existing resources, an extra profile HTML resource, a
readme resource and a different signature. Signature presence has not been
cryptographically verified. Historical locator declarations do not mean those
servers remain available or that any downloads were executed.

Notably the Crusades manifest advertises a 6923-byte v3readme while the extracted
payload's file is 7113 bytes. The manifests therefore cannot be treated as an
exact inventory of the payload or proof of a completed installation. Installer
conditions and update precedence still need separate analysis.

The metagame bundle lists Darien.def and both JJE tables. Searches of both
manifests found no MetaMask, Borders or HonorMap resource names. These images
are now independently verified in the fingerprinted GOG distribution and match
the current local installation. The original CD cabinet also lacks these loose
images; the historical Boneyards delivery route remains unresolved. See
[distribution provenance](distribution-provenance.md). Authentic artwork and
its native hit-test use still do not establish authoritative territory adjacency.

To inspect a user-supplied file offline, Python's standard `zipfile.ZipFile` can
list members and read `Kingdoms.mmm` without extracting paths or executing
anything. Original manifest text remains outside Git; committed inventories
already fingerprint the compressed files.
