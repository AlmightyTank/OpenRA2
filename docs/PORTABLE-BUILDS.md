# GitHub portable builds — Windows, Android, iPhone/iPad

The **Portable builds** workflow (`.github/workflows/portable-builds.yml`)
creates downloadable GitHub Actions **artifacts** on each push to
`feature/android-gamepad-v0.1`, or when you select **Actions →
Portable builds → Run workflow** for that branch.

No player game archives are distributed. Every platform must import or
point to legitimately obtained **Red Alert 2: Yuri's Revenge** game data
(`ra2.mix`, `ra2md.mix`, `language.mix`, etc.).

| Job | Output artifact | Device requirements |
|---|---|---|
| Windows portable | `OpenRA2-Windows-portable-<sha>` with a `ra2engine-*-win64.zip` | Windows x64; extract ZIP; run `ra2game.exe` |
| Android APK | `OpenRA2-Android-debug-<sha>` with a `.apk` | Android 5.0+, arm64-v8a or x86_64; import your files on first launch |
| iOS unsigned app | `OpenRA2-iOS-unsigned-<sha>` with a `.app.zip` | iPhone/iPad app bundle for **device** ARM64; not directly installable without Apple signing |

## Download a build

1. Open the repository's **Actions** tab.
2. Choose **Portable builds**.
3. Open a **successful workflow run** for the desired branch/commit.
4. Scroll to **Artifacts**, download the matching platform, and extract the
   outer GitHub artifact ZIP if needed.
5. The Windows ZIP contains the portable folder. The Android APK can be
   sideloaded. The iOS ZIP contains the unsigned `.app` bundle (not a signed IPA).

Workflow artifacts are retained for **14 days**. The workflow does **not**
automatically publish a public GitHub Release or upload to app stores.

## Platform builds

### Windows portable

The workflow uses MinGW-w64 on a hosted Windows runner and the existing
`scripts/package-win.ps1 -Configure` release packager.

The package script verifies imported DLLs and executes
`ra2engine.exe --version` from a clean Windows PATH. If that check fails,
the job fails and no success is claimed. No external installer is necessary.
The players' game data remains separate.

### Android

Uses JDK 21, SDK 35, NDK **27.2.12479018** (r27c), CMake **3.22.1**, and
the repository's Gradle 8.9 wrapper. Produces the existing
`app-debug.apk` as a **debug-signed testing build**.

**Important:** The GitHub-hosted runner's debug keystore is ephemeral.
A subsequent workflow's APK will generally have a different signing
certificate. Android may require uninstalling the older debug APK before
installing the next one, which can **delete app data/saves**.
For consistent upgrades, configure a private, stable release keystore and
a separately authorized release-signing workflow before distributing broadly.
Never put keystores/passwords in the repository or chat.

### iPhone / iPad

Uses GitHub's macOS runner with Xcode, Homebrew xtool, and the repository's
existing `ios/scripts/build.sh --release --no-game-data` SwiftPM build.
The upload is a compressed `RA2Engine.app` directory with
`CFBundleIdentifier=org.ra2.engine` and no copyrighted game assets.

**This is NOT a signed, installable IPA.** GitHub-hosted CI cannot provision
an actual device under your Apple team automatically without signing assets,
developer account access, and a provisioning profile. Use the existing
`ios/scripts/build.sh --install --no-game-data` on your own properly
configured machine to sign/install, or later configure secure CI signing
through GitHub Actions secrets. Do not add Apple IDs, passwords, private
keys, or provisioning profiles to the source repository.

The existing SwiftPM iOS target currently builds **physical ARM64 iOS**,
not the iOS Simulator. A simulator-specific target is future work.

## CI rules and limits

- Builds run from the selected branch and immutable source commit.
- Each job uploads only its own output and fails on missing binaries.
- A passing build proves compilation/package checks, **not** in-game
  stability on a physical device or functional parity across platforms.
- None of the jobs alters `master`, publishes a release, signs an iOS
  application, or deploys a multiplayer server.
- Windows and Android builds require downloading build tools and SDL2;
  network or runner capacity problems are reported as build failures.

## Progression

Once all jobs are green, validate the Windows portable package in a clean
Windows VM, sideload the Android APK on a real device and test controls,
then configure iOS signing privately so an installable IPA can be created.
