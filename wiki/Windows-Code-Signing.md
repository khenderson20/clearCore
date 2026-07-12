# <img src="assets/azure-icons/key-vaults.svg" width="30" align="top" alt=""> Windows Code Signing

clearCore's Windows binaries are signed with **[Azure Artifact Signing](https://learn.microsoft.com/azure/artifact-signing/)** so that Windows Defender and SmartScreen trust them. This page explains why signing is required, what the release workflow does automatically, and the one-time setup a maintainer must complete for signing to activate.

> **Naming note.** This service was called **Trusted Signing** during preview and was renamed **Artifact Signing** at general availability (January 2026). The rename touched the account type, the RBAC roles, the docs URL, and the GitHub Action, but **not** the Azure resource provider (`Microsoft.CodeSigning`) or the signing endpoints (`*.codesigning.azure.net`). Where this page says "Artifact Signing," older tutorials may still say "Trusted Signing" — they mean the same service.

---

## <img src="assets/azure-icons/defender-for-cloud.svg" width="26" align="top" alt=""> Why signing is required

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

## <img src="assets/azure-icons/workflow.svg" width="26" align="top" alt=""> What the release workflow does

Signing lives in the `windows-x64` job of [`cross-platform.yml`](https://github.com/khenderson20/clearCore/blob/main/.github/workflows/cross-platform.yml) and runs in two passes:

1. **Before `cpack`** — sign the three application executables in the build tree:
   `clearCore-gui.exe`, `clearCore-quick.exe`, `number_system_converter.exe`.
   `cpack` then copies the already-signed binaries into the NSIS package. This pass is what clears the ASR block on the installed app. (Qt's own runtime DLLs bundled by `windeployqt` are already signed upstream by The Qt Company.)
2. **After `cpack`** — sign the generated NSIS installer, so SmartScreen trusts the download and drops the "unknown publisher" wall.

Both passes use the **`azure/artifact-signing-action`** action, pinned to a commit SHA. The workflow still references it under its former path `azure/trusted-signing-action` — GitHub redirects that to the renamed repository, and it resolves to the same commit (`v2.0.0`), so the pin keeps working unchanged. The action still accepts the legacy `trusted-signing-account-name` input alongside the newer `signing-account-name`; both map to the same value.

Authentication uses **OIDC federated login** (`azure/login`) — no client secret is stored in the repo. The job is scoped to a `release-signing` GitHub Environment, which gives the OIDC token a stable subject (`repo:khenderson20/clearCore:environment:release-signing`) so a single Entra federated credential covers every release regardless of tag name.

Every signing step is gated on `vars.AZURE_SIGNING_ACCOUNT != ''`. **Until the setup below is complete, builds still succeed and ship unsigned** — nothing breaks; the binaries simply carry no signature.

---

## <img src="assets/azure-icons/toolbox.svg" width="26" align="top" alt=""> One-time setup

Signing activates only after all of the following are in place.

### <img src="assets/azure-icons/key-vaults.svg" width="22" align="top" alt=""> 1. Azure Artifact Signing

> **A paid Azure subscription is required.** Free, trial, and sponsored subscriptions cannot create Artifact Signing resources. The resource provider `Microsoft.CodeSigning` must also be registered on the subscription (Subscription → Resource providers → **Microsoft.CodeSigning** → Register).

1. Create an **Artifact Signing account** in the Azure portal. Note its region **endpoint**, e.g. `https://eus.codesigning.azure.net` (see the [region → endpoint table](https://learn.microsoft.com/azure/artifact-signing/quickstart#azure-regions-that-support-artifact-signing)).
2. Complete **identity validation**. Creating a validation request requires the **Artifact Signing Identity Verifier** role on the account.

   ⚠️ **This is the long pole**, and its length depends on the trust type:

   - **Public Trust** requires a validated identity. **Organization** validation is available to organizations in the USA, Canada, the EU, and the UK, and can take 1–20 business days (longer if extra documents are requested). **Individual** validation is available to individual developers in the USA and Canada; it is done through a Microsoft **Verified ID** flow (a third-party identity check via the Microsoft Authenticator app) rather than the multi-year business-history barrier of the past.
   - **Private Trust** does not require Public Trust identity validation and isn't geographically limited, but Windows will not trust the resulting signatures out of the box — it's only useful with your own deployed root, so it's **not** what clears the ASR/SmartScreen block for public downloads.

   Start this early.
3. Create a **Public Trust** certificate profile. Note its **name**. (CN and O are taken from the validated identity — custom CN/O values are not supported; EV certificates are not issued.)

### <img src="assets/azure-icons/app-registrations.svg" width="22" align="top" alt=""> 2. Entra service principal + federated credential

**Step 1 — Create the app registration.** Create an **Entra app registration** (service principal). Note its **client ID**, **tenant ID**, and the target **subscription ID** — these go into the GitHub secrets in section 3.

**Step 2 — Assign the signing role.** Grant the app the **Artifact Signing Certificate Profile Signer** role on the Artifact Signing account. This authorizes the app to sign; the federated credential in step 3 only handles *login*.

1. In the Azure portal, open your **Artifact Signing account**.
2. In the left menu, select **Access control (IAM)**.
3. Select **Add → Add role assignment**.
4. On the **Role** tab, search for **Artifact Signing Certificate Profile Signer**, select it, then **Next**.
5. On the **Members** tab, leave **Assign access to** as **User, group, or service principal**, select **+ Select members**, search for the app registration by name (e.g. *Code Sign Github*), select it, and click **Select**.
6. Select **Review + assign** (twice) to finish.

> The role can also be scoped to the resource group or a specific certificate profile instead of the whole account. Account scope is simplest; narrower scope works as long as it covers the profile named in `AZURE_SIGNING_PROFILE`. Role assignments can take a few minutes to propagate before signing succeeds.

**Step 3 — Add a federated credential** so GitHub Actions can log in over OIDC without a stored secret. On the app registration, go to **Certificates & secrets → Federated credentials → Add credential**, choose the **GitHub Actions deploying Azure resources** scenario, and enter:

- **Issuer** — `https://token.actions.githubusercontent.com` (auto-filled)
- **Organization** — `khenderson20` (the repo owner; a personal account works here — no GitHub org is required)
- **Repository** — `clearCore`
- **Entity type** — **Environment**
- **GitHub environment name** — `release-signing`
- **Subject identifier** — auto-fills to `repo:khenderson20/clearCore:environment:release-signing`; confirm it matches exactly
- **Name** — `clearCore-release-signing` (immutable after creation)
- **Description** — `OIDC login for signing Windows release binaries via GitHub Actions.`

> **Entity type must be Environment**, not Branch or Tag — the `windows-x64` job runs under `environment: release-signing`, so its OIDC token carries the `:environment:release-signing` subject. A Branch-scoped credential would produce a different subject and `azure/login` would fail with a mismatch.

### <img src="assets/azure-icons/gear.svg" width="22" align="top" alt=""> 3. GitHub repository configuration

Under **Settings → Secrets and variables → Actions**:

| Kind     | Name                     | Value                                              |
|----------|--------------------------|----------------------------------------------------|
| Secret   | `AZURE_CLIENT_ID`        | Entra app (client) ID                              |
| Secret   | `AZURE_TENANT_ID`        | Entra directory (tenant) ID                        |
| Secret   | `AZURE_SUBSCRIPTION_ID`  | Azure subscription ID                              |
| Variable | `AZURE_SIGNING_ENDPOINT` | Artifact Signing endpoint URL                      |
| Variable | `AZURE_SIGNING_ACCOUNT`  | Artifact Signing account name                      |
| Variable | `AZURE_SIGNING_PROFILE`  | Certificate profile name                           |

Then create the **`release-signing` environment** under **Settings → Environments**. Leave it without protection rules, or add required reviewers if you want a manual approval gate before signed releases are produced.

---

## <img src="assets/azure-icons/builds.svg" width="26" align="top" alt=""> Testing the setup

Signing runs on both published releases and manual `workflow_dispatch` runs. To validate the configuration **before** cutting a real release, trigger the workflow manually:

```
Actions → Cross-platform build → Run workflow
```

Download the resulting Windows artifact and confirm the signature — either via file **Properties → Digital Signatures**, or in PowerShell:

```powershell
Get-AuthenticodeSignature "clearCore-gui.exe" | Format-List Status, SignerCertificate
```

A `Status` of `Valid` means the binary is trusted; installing and launching it should no longer produce "Access is denied", and no Defender ASR exclusion is needed.

> **SmartScreen reputation is separate.** A valid Authenticode signature clears the ASR block immediately, but the SmartScreen "unknown publisher" prompt on downloads fades only as the signed file's hash accrues download history. This builds automatically over time; nothing more is required in the workflow.

---

## <img src="assets/azure-icons/troubleshoot.svg" width="26" align="top" alt=""> Troubleshooting

- **The signing steps are skipped entirely.** `vars.AZURE_SIGNING_ACCOUNT` is empty — the variable isn't set. Confirm all three *variables* exist under Actions → Variables (not Secrets).
- **`azure/login` fails with an OIDC / federated-credential error.** The federated credential subject must be exactly `repo:khenderson20/clearCore:environment:release-signing`, and the job must reference the `release-signing` environment. A mismatch here is the most common failure.
- **Signing fails with a `403` / authorization error.** Usually the service principal is missing the **Artifact Signing Certificate Profile Signer** role on the account. Also confirm the resource provider shows **Registered**, the account/profile names in the workflow variables are correct, and the identity validation status is **Completed** with the certificate profile **active**.
- **The `New identity validation` button is greyed out in the Azure portal.** The signed-in user lacks the **Artifact Signing Identity Verifier** role on the account.
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
- [Azure Artifact Signing quickstart](https://learn.microsoft.com/azure/artifact-signing/quickstart) — official setup walkthrough
- [Azure/artifact-signing-action](https://github.com/Azure/artifact-signing-action) — the GitHub Action used by the release workflow
