# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| `0.1.x` | Yes |
| Older research builds | No |

## Scope and safe use

This project injects a DLL into a locally launched CS2 process and therefore must be treated as high-risk native software.

- Use it only for local Demo playback with `-insecure`.
- Never connect to VAC-secured servers, matchmaking, or other online games while the DLL is loaded.
- Download binaries only from this repository's GitHub Releases page and verify the published SHA-256 checksum.
- A CS2 update invalidates the supported build until the PE fingerprints, fixed RVAs, entry bytes, and regression checks are audited again.

The project intentionally refuses fixed-RVA client work when the supported `engine2.dll` or `client.dll` fingerprint does not match.

## Reporting a vulnerability

Please use GitHub's private vulnerability reporting feature under the repository's **Security** tab. Do not publish exploitable details, unsafe offset changes, or bypasses in a public issue before the report has been assessed.

Include the affected version, Windows version, CS2 module fingerprints, reproduction steps, and the smallest relevant log excerpt. Do not include Steam credentials or other private data.
