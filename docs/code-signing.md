# Code signing: getting rid of the "virus" warnings for good

Windows warns about any exe that isn't **signed**: the blue SmartScreen box, Smart App Control
blocking it, and sometimes a Defender "threat" guess. The game is safe; Windows just doesn't
know who made it. A signature tells Windows who did.

The cheapest real option for one person in Canada is **Azure Artifact Signing** (Microsoft's
service, formerly "Trusted Signing"): **US$9.99 a month**, open to individual developers in the
USA and Canada. Once it's set up, the GitHub workflow (`.github/workflows/windows-build.yml`)
signs every build by itself.

## Set it up (about an hour, most of it waiting for the ID check)
1. **Azure account:** <https://portal.azure.com> (a credit card is needed for the $9.99/month).
2. **Create an Artifact Signing account:** search "Artifact Signing" in the portal → Create.
   Pick a region (for example East US) and note the **endpoint** it shows, e.g.
   `https://eus.codesigning.azure.net/`.
3. **Identity validation:** in the account → Identity validations → New → **Individual**. You
   verify your ID with Microsoft's partner (photo ID). This can take a few days.
4. **Certificate profile:** once validated → Certificate profiles → Create → **Public Trust**,
   linked to your validation. Note the profile name.
5. **An app login for GitHub:** Microsoft Entra ID → App registrations → New registration
   (name: `blockfall-signing`). Note the **Directory (tenant) ID** and **Application (client)
   ID**. Certificates & secrets → New client secret → copy the **value**.
6. **Allow it to sign:** in your Artifact Signing account → Access control (IAM) → Add role
   assignment → **Artifact Signing Certificate Profile Signer** → the `blockfall-signing` app.
7. **GitHub secrets:** your repo → Settings → Secrets and variables → Actions → New repository
   secret, six times:

   | Secret | Value |
   |---|---|
   | `AZURE_TENANT_ID` | Directory (tenant) ID |
   | `AZURE_CLIENT_ID` | Application (client) ID |
   | `AZURE_CLIENT_SECRET` | the client secret value |
   | `SIGNING_ENDPOINT` | e.g. `https://eus.codesigning.azure.net/` |
   | `SIGNING_ACCOUNT` | your Artifact Signing account name |
   | `SIGNING_PROFILE` | your certificate profile name |

8. **Run it:** repo → Actions → *Windows build* → **Run workflow**. A minute or two later
   `bin/Blockfall.exe` is replaced with a signed build (the commit says "signed MSVC build").

## Good to know
- New signatures still need a little "reputation" with SmartScreen; a signed exe with a known
  publisher clears it much faster than an unsigned one, and Smart App Control allows signed apps.
- If Defender ever flags a build, send it at <https://www.microsoft.com/wdsi/filesubmission>
  as a false positive.
- Without the secrets, the workflow still builds with Microsoft's compiler (which antivirus
  guess-checks flag less often than the MinGW build) and commits that unsigned exe.
