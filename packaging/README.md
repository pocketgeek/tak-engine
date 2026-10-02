# Application icons

The application icon is our procedural crown from `src/util/appicon.cpp`, not a
retail asset. Regenerate the checked-in Windows and macOS icon containers with:

```sh
cmake --build build --target makeicons
python3 tools/make-platform-icons.py build/makeicons
```

The generator uses Python's standard library and the existing C++ renderer. It
adds no runtime dependency or normal-build requirement. Windows embeds the ICO
in `takclient.exe` through a compiled resource, and NSIS uses it for the installer
and uninstaller. macOS copies the ICNS into `Contents/Resources` and declares it
in `Info.plist`.

The macOS bundle retains its required `.app` suffix. Finder controls whether
extensions are displayed. Do not add FinderInfo attributes to the signed bundle:
strict code-signature verification rejects them. The ZIP is created with `ditto`
and distributed intact. Native CI validates the ICNS with `iconutil` and verifies
the bundle signature before packaging.

## Windows release signing

Official `pocketgeek/tak-engine` `v*` tag builds use Azure Artifact Signing via
GitHub OIDC. Both native builds and their tests must pass before a separate
`windows-2022` x64 job downloads their same-run executable artifacts. The signing
action does not support ARM64 runners; ARM64 executables are signed on x64
without rebuilding or running them.

The job uses the **windows-signing** environment, whose deployment rule must
allow **tags matching `v*` only** (no branches). Its environment secrets are
`AZURE_CLIENT_ID`, `AZURE_TENANT_ID`, and `AZURE_SUBSCRIPTION_ID`; no Azure client
secret or certificate private key is stored in GitHub. Its variables are:

| Variable | Value |
| --- | --- |
| `AZURE_SIGNING_ACCOUNT_NAME` | `Githubsign` |
| `AZURE_SIGNING_CERTIFICATE_PROFILE_NAME` | `tak-engine` |
| `AZURE_SIGNING_ENDPOINT` | `https://eus.codesigning.azure.net/` |

The Azure federated credential should trust issuer
`https://token.actions.githubusercontent.com`, audience `api://AzureADTokenExchange`,
and subject `repo:pocketgeek/tak-engine:environment:windows-signing`. The service
principal needs the **Artifact Signing Certificate Profile Signer** role on the
signing profile/account. Only the release signing job receives `id-token: write`.
The environment's tag restriction is required because an environment-based OIDC
subject identifies the environment, not a specific branch or tag.

All four executables (`takclient`, `takserver`, `cartographer`, `crusades_admin`)
in both Release and Debug, for x64 and ARM64, are signed with SHA-256 and an
RFC 3161 SHA-256 timestamp. Authenticode and Windows SDK SignTool verify signatures,
certificate chains and timestamps before ZIP/NSIS packaging. Packaging checks
that ZIP entries and the NSIS staged payload retain the signed bytes. The
completed x64 installer is then signed and verified before anything is attached
to the official release. A missing signing setting, failed signing request,
missing timestamp or verification error fails the job; it cannot publish unsigned
Windows release assets.

Main builds and manual branch builds remain unsigned and require no Azure
configuration. Forks retain unsigned builds and tag packaging without entering
the signing environment. Native build artifacts are unsigned intermediate CI
outputs; official release downloads come only from the verified signing job.
The shared `windows/installer.cmake` keeps ordinary and signed NSIS packaging
consistent. This adds no application runtime dependency.

References: [Azure signing action and runner requirements](https://github.com/Azure/artifact-signing-action),
[OIDC setup](https://github.com/Azure/artifact-signing-action/blob/main/docs/OIDC.md),
and [SignTool verification](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).
