# Release checklist — arksh

This checklist must be completed for every tagged release (patch, minor, or major).
Work through it top-to-bottom; do not skip steps.

---

## 1. Pre-release validation

- [ ] All CTest tests pass locally on the release branch: `cmake --build build && ctest --test-dir build -j$(nproc)`
- [ ] Test matrix green on GitHub Actions (Linux + macOS + Windows)
- [ ] ASan + UBSan build clean: `cmake -DCMAKE_BUILD_TYPE=Debug -DARKSH_SANITIZE=ON … && ctest`
- [ ] Startup wall-time guard: `arksh_perf_startup_wall_drop` passes (`wall_ms <= 50`)
- [ ] No open regressions tagged `release-blocker` in the issue tracker

## 2. Version bump

- [ ] Update `ARKSH_VERSION_MAJOR/MINOR/PATCH` in [CMakeLists.txt](../CMakeLists.txt)
- [ ] Update `PackageVersion` in [packaging/winget/manifests/A/Arksh/Arksh/0.1.0/Arksh.Arksh.yaml](../packaging/winget/manifests/A/Arksh/Arksh/0.1.0/Arksh.Arksh.yaml) (copy the folder to the new version)
- [ ] Update `# version` comment in [Formula/arksh.rb](../Formula/arksh.rb) and uncomment the stable `url`/`sha256` block

## 3. Changelog

- [ ] Move items from `## [Unreleased]` to a new `## [X.Y.Z] — YYYY-MM-DD` section in [CHANGELOG.md](../CHANGELOG.md)
- [ ] Update the comparison URLs at the bottom of CHANGELOG.md
- [ ] Review the entry for clarity and completeness — no placeholder text

## 4. Build release artifacts

### Linux
```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-release -j$(nproc)
cd build-release
cpack -G DEB   # → arksh-X.Y.Z-Linux.deb
cpack -G RPM   # → arksh-X.Y.Z-Linux.rpm
cpack -G TGZ   # → arksh-X.Y.Z-Linux.tar.gz
```

### macOS
```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-release
# Produces bin/arksh — package as .tar.gz or .zip for the GitHub release asset
```

### Windows (PowerShell)
```powershell
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-release --config Release
# Compress bin/Release/arksh.exe into arksh-X.Y.Z-win64.zip
```

## 5. Compute checksums

```bash
# Linux
sha256sum arksh-X.Y.Z-Linux.deb arksh-X.Y.Z-Linux.rpm arksh-X.Y.Z-Linux.tar.gz

# macOS
shasum -a 256 arksh-X.Y.Z-macos.tar.gz

# Windows (PowerShell)
Get-FileHash arksh-X.Y.Z-win64.zip -Algorithm SHA256
```

- [ ] Record all checksums in `docs/release-notes-X.Y.Z.md` (or in the GitHub release body)

## 6. Tag and push

```bash
git tag -s vX.Y.Z -m "Release vX.Y.Z"
git push origin vX.Y.Z
```

- [ ] Tag signed (or annotated if GPG not available)
- [ ] Push triggers GitHub Actions release matrix

## 7. GitHub release

- [ ] Create a GitHub release from the tag
- [ ] Attach all artifacts:
  - `arksh-X.Y.Z-Linux.deb`
  - `arksh-X.Y.Z-Linux.rpm`
  - `arksh-X.Y.Z-Linux.tar.gz`
  - `arksh-X.Y.Z-macos.tar.gz` (or `.zip`)
  - `arksh-X.Y.Z-win64.zip`
- [ ] Paste CHANGELOG section as the release body
- [ ] Mark as pre-release if version < 1.0.0

## 8. Update packaging manifests

### Homebrew
- [ ] Update `sha256` and `url` in [Formula/arksh.rb](../Formula/arksh.rb) to point to the macOS tarball
- [ ] `brew install --build-from-source ./Formula/arksh.rb` — verify it builds and `brew test` passes
- [ ] Open a PR to `nicolorisitano82/homebrew-arksh` (or update the tap in-repo)

### winget
- [ ] Copy `packaging/winget/manifests/A/Arksh/Arksh/0.1.0/` to the new version folder
- [ ] Update `InstallerUrl` and `InstallerSha256` in the installer YAML with the Windows zip values
- [ ] Run `winget validate --manifest packaging/winget/manifests/A/Arksh/Arksh/X.Y.Z/`
- [ ] Fork [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs), copy the manifest folder, open a PR

## 9. Documentation site

- [ ] `mkdocs build --strict` — no warnings
- [ ] Push to `main` to trigger the GitHub Pages deploy workflow
- [ ] Verify the published site renders correctly at `https://nicolorisitano82.github.io/arksh/`

## 10. Post-release

- [ ] Open a new `## [Unreleased]` section in CHANGELOG.md and push
- [ ] Announce in the project discussion / social channels if applicable
- [ ] Close the milestone for this release on GitHub

---

## Hotfix procedure

1. Branch off the release tag: `git checkout -b hotfix/X.Y.Z+1 vX.Y.Z`
2. Apply the fix, run the full CTest suite
3. Bump the patch version and update CHANGELOG
4. Tag and follow steps 6–10 above
