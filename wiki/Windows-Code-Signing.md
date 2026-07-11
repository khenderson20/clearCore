# Windows Code Signing

clearCore's Windows binaries are signed with **[Azure Trusted Signing](https://learn.microsoft.com/azure/trusted-signing/)** so that Windows Defender and SmartScreen trust them. This page explains why signing is required, what the release workflow does automatically, and the one-time setup a maintainer must complete for signing to activate.

---

## Why signing is required

Windows Defender ships an **Attack Surface Reduction (ASR)** rule — *"Block executable files from running unless they meet a prevalence, age, or trusted-list criterion"* — that refuses to launch freshly built, unsigned, low-prevalence executables. A binary produced by CI and downloaded minutes ago is the textbook trigger.

The failure is easy to misread because the symptom is generic:

```
Program 'clearCore-gui.exe' failed to run: An error occurred trying to
start process ... Access is denied
```

or, when launched from Explorer:

> Windows cannot access the specified device, path, or file. You may not
> have the appropriate permissions to access the item.

This is **not** a missing-DLL error (that reads *"The code execution cannot proceed because Qt6Core.dll was not found"*) and **not** a packaging bug. It is the OS refusing to execute an untrusted binary.

Crucially, the ASR rule inspects the **installed** `clearCore-gui.exe`, not just the installer — so signing only the NSIS installer would not fix it. Both the application executables **and** the installer must be signed.

---

## What the release workflow does

Signing lives in the `windows-x64` job of [`cross-platform.yml`](https://github.com/khenderson20/clearCore/blob/main/.github/workflows/cross-platform.yml) and runs in two passes:

1. **Before `cpack`** — sign the three application executables in the build tree:
   `clearCore-gui.exe`, `clearCore-quick.exe`, `number_system_converter.exe`.
   `cpack` then copies the already-signed binaries into the NSIS package. This pass is what clears the ASR block on the installed app. (Qt's own runtime DLLs bundled by `windeployqt` are already signed upstream by The Qt Company.)
2. **After `cpack`** — sign the generated NSIS installer, so SmartScreen trusts the download and drops the "unknown publisher" wall.

Authentication uses **OIDC federated login** (`azure/login`) — no client secret is stored in the repo. The job is scoped to a `release-signing` GitHub Environment, which gives the OIDC token a stable subject (`repo:khenderson20/clearCore:environment:release-signing`) so a single Entra federated credential covers every release regardless of tag name.

Every signing step is gated on `vars.AZURE_SIGNING_ACCOUNT != ''`. **Until the setup below is complete, builds still succeed and ship unsigned** — nothing breaks; the binaries simply carry no signature.

---

## One-time setup

Signing activates only after all of the following are in place.

### 1. Azure Trusted Signing

1. Create a **Trusted Signing account** in the Azure portal. Note its region **endpoint**, e.g. `https://eus.codesigning.azure.net/`.
2. Complete **identity validation**.
   > ⚠️ This is the long pole. *Public Trust* requires a verified identity — organization validation if clearCore is under a legal entity, or **individual** validation, which Microsoft historically requires several years of verifiable history for. Start this early.
3. Create a **Public Trust certificate profile**. Note its **name**.

### 2. Entra service principal + federated credential

1. Create an **Entra app registration** (service principal).
2. Grant it the **"Trusted Signing Certificate Profile Signer"** role on the Trusted Signing account.
3. Add a **federated credential** for GitHub Actions with the subject:
   ```
   repo:khenderson20/clearCore:environment:release-signing
   ```

### 3. GitHub repository configuration

Under **Settings → Secrets and variables → Actions**:

| Kind     | Name                     | Value                                              |
|----------|--------------------------|----------------------------------------------------|
| Secret   | `AZURE_CLIENT_ID`        | Entra app (client) ID                              |
| Secret   | `AZURE_TENANT_ID`        | Entra directory (tenant) ID                        |
| Secret   | `AZURE_SUBSCRIPTION_ID`  | Azure subscription ID                              |
| Variable | `AZURE_SIGNING_ENDPOINT` | Trusted Signing endpoint URL                       |
| Variable | `AZURE_SIGNING_ACCOUNT`  | Trusted Signing account name                       |
| Variable | `AZURE_SIGNING_PROFILE`  | Certificate profile name                           |

Then create the **`release-signing` environment** under **Settings → Environments**. Leave it without protection rules, or add required reviewers if you want a manual approval gate before signed releases are produced.

---

## Testing the setup

Signing runs on both published releases and manual `workflow_dispatch` runs. To validate the configuration **before** cutting a real release, trigger the workflow manually:

```
Actions → Cross-platform build → Run workflow
```

Download the resulting Windows artifact and confirm the signature — either via file **Properties → Digital Signatures**, or in PowerShell:

```powershell
Get-AuthenticodeSignature "clearCore-gui.exe" | Format-List Status, SignerCertificate
```

A `Status` of `Valid` means the binary is trusted; installing and launching it should no longer produce "Access is denied", and no Defender ASR exclusion is needed.

---

## Troubleshooting

- **The signing steps are skipped entirely.** `vars.AZURE_SIGNING_ACCOUNT` is empty — the variable isn't set. Confirm all three *variables* exist under Actions → Variables (not Secrets).
- **`azure/login` fails with an OIDC / federated-credential error.** The federated credential subject must be exactly `repo:khenderson20/clearCore:environment:release-signing`, and the job must reference the `release-signing` environment. A mismatch here is the most common failure.
- **Signing fails with an authorization error.** The service principal is missing the **Trusted Signing Certificate Profile Signer** role on the account.
- **A developer hits "Access is denied" locally on an unsigned dev build.** For a personal machine only, add a temporary Defender exclusion from an **admin** PowerShell, then remove it once signed builds are available:
  ```powershell
  Add-MpPreference -AttackSurfaceReductionOnlyExclusions "C:\Program Files\clearCore\bin\clearCore-gui.exe"
  # later:
  Remove-MpPreference -AttackSurfaceReductionOnlyExclusions "C:\Program Files\clearCore\bin\clearCore-gui.exe"
  ```
  This is a local workaround only — it does nothing for end users, which is why the binaries are signed in CI instead.

---

## See also

- [Getting Started](Getting-Started) — building and running from source
- [Contributing](Contributing) — PR workflow and CI overview
