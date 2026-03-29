# arksh — winget packaging

## How to install (once the package is in winget-pkgs)

```powershell
winget install Arksh.Arksh
```

## How to submit to winget-pkgs

1. Build a Release from GitHub Actions (Windows matrix job).
2. Attach `arksh-<version>-win64.zip` to the GitHub release.
3. Compute its SHA-256:
   ```powershell
   Get-FileHash arksh-0.1.0-win64.zip -Algorithm SHA256
   ```
4. Update `InstallerSha256` and `InstallerUrl` in
   `manifests/A/Arksh/Arksh/<version>/Arksh.Arksh.installer.yaml`.
5. Fork [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs),
   copy the `manifests/A/Arksh/Arksh/<version>/` folder into the same
   path in that repo, and open a pull request.

## Local testing

```powershell
# Validate manifest locally
winget validate --manifest manifests/A/Arksh/Arksh/0.1.0/

# Install from local manifest (requires Developer Mode or admin)
winget install --manifest manifests/A/Arksh/Arksh/0.1.0/
```
