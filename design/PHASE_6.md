# Phase 6 Packaging and Deployment

`VERSION` supplies the VST3 version, web asset version, Android version name, and artifact filenames. Increase Android's integer `versionCode` for each Android update. Version 0.6.0 uses code 7. A release tag must match `VERSION` exactly.

## Artifacts

| Filename | Purpose |
| --- | --- |
| `Ocarina-of-Resonance-Windows-vX.Y.Z.vst3.zip` | Windows x64 VST3 bundle |
| `Ocarina-of-Resonance-Android-vX.Y.Z.apk` | Signed ARM64 standalone APK |
| `Ocarina-of-Resonance-Android-vX.Y.Z.aab` | Signed ARM64 Android App Bundle |
| `Ocarina-of-Resonance-Web-vX.Y.Z.zip` | Tested static web output with Wasm and AudioWorklet |
| `SHA256SUMS.txt` | SHA-256 checksums of all four files |

CI also uploads a versioned ARM64 debug APK under **Android-ARM64-debug**. Its package ID is `com.blinkzer0.ocarina.debug`, so it can coexist with the release package `com.blinkzer0.ocarina`.

## Local packages

Use the Windows and web build commands in the [README](../README.md). Use JDK 21, Android SDK platform 36, build-tools 36.0.0, NDK 29.0.14206865, CMake 3.31.6, and Python 3.11 or newer for the packaging commands. Run the following from the repository root after building the web assets.

```powershell
.\build.bat
.\android\build.bat assembleDebug
py scripts/package-release.py windows "build/windows/src/plugin/OcarinaVST_artefacts/Release/VST3/Ocarina of Resonance.vst3" dist/v0.6.0
py scripts/package-release.py web docs dist/v0.6.0
py scripts/package-release.py android debug --apk android/app/build/outputs/apk/debug/app-debug.apk --output dist/v0.6.0
```

For a signed local build, create `android/signing.properties`, which Git ignores. Use a private keystore outside the repository and forward slashes in Windows paths. The property names are below.

```properties
storeFile=C:/private/ocarina-release.jks
storePassword=<keystore password>
keyAlias=<release key alias>
keyPassword=<key password>
```

Alternatively set all four environment variables in the build process. Use `ANDROID_RELEASE_KEYSTORE` for the local keystore path and `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, and `ANDROID_KEY_PASSWORD` for its credentials. The build reads the environment as a complete configuration when any of these variables is set. Keep private keys and passwords in private storage.

```powershell
.\android\build.bat --no-daemon assembleRelease bundleRelease
py scripts/package-release.py bundletool build/tools
py scripts/package-release.py android release --apk android/app/build/outputs/apk/release/app-release.apk --aab android/app/build/outputs/bundle/release/app-release.aab --bundletool build/tools/bundletool-all-1.18.3.jar --output dist/v0.6.0
py scripts/package-release.py checksums dist/v0.6.0
```

On Linux or macOS use `sh android/gradlew -p android` and `python3` in the corresponding commands. To pin verification to your expected certificate add `--certificate-sha256` with its public fingerprint. The verifier checks app IDs and versions, shared panel assets and notices, APK signature and ZIP alignment, ARM64 ELF load alignment, AAB structure and page-alignment settings, and matching certificates across the two Android formats. [Third-party notices](../licenses/README.md) · [Android 16 KB page sizes](https://developer.android.com/guide/practices/page-sizes) · [Bundletool](https://developer.android.com/tools/bundletool)

## GitHub release

The **Package and release Ocarina of Resonance** workflow can be run manually as a rehearsal. It builds and tests all three platforms, uploads the Windows and web packages and their checksums, and uploads the debug APK separately.

For a public release, add these environment secrets in repository **Settings → Environments → release-signing**. The Base64 value represents the keystore file bytes and is as sensitive as the file.

| Secret | Value |
| --- | --- |
| `ANDROID_KEYSTORE_BASE64` | Base64-encoded private release keystore |
| `ANDROID_KEYSTORE_PASSWORD` | Keystore password |
| `ANDROID_KEY_ALIAS` | Release key alias |
| `ANDROID_KEY_PASSWORD` | Release key password |

Allow matching release tags in the `release-signing` and `github-pages` deployment environments. Keep the existing Pages development branch policy. Use the environment's reviewer controls when available. Build the signed candidate locally and exercise the APK and AAB on the device before pushing the version tag. Update the [public release notes](RELEASE_NOTES.md), run the rehearsal, then tag the verified commit. The tag workflow checks the version before building, signs the Android outputs with a temporary keystore on the runner, verifies the packages, and deploys Pages from the same tested web output. GitHub creates the public Release after these jobs succeed. [GitHub deployment environments](https://docs.github.com/en/actions/deployment/targeting-different-environments/managing-environments-for-deployment) · [GitHub release publication](https://cli.github.com/manual/gh_release_create)

## Direct install and Google Play

On a compatible ARM64 device running Android 8 or newer, download the signed APK and open it with Android's package installer. The system may prompt you to allow installations from the browser or file manager you used. Keep the same app-signing key and increment `versionCode` to provide updates to existing direct installs.

Google Play accepts an AAB signed with an upload key and signs the APKs it delivers with the Play app-signing key. If upgrades between Play and direct downloads are important, decide how the app-signing key will be shared before enrolling. A different signing certificate prevents one build from updating an installed copy signed with the other certificate. [Android app signing](https://developer.android.com/studio/publish/app-signing)

Google's current developer-verification protections apply to participating stores in selected regions. Google announced an expansion beginning in January 2027 that will gradually reach other installation sources. Review the rollout and register the production package and signing certificate as applicable before distributing. [Android developer verification](https://developer.android.com/developer-verification) · [2027 rollout](https://developer.android.com/blog/posts/android-developer-verification-2027)

The Windows ZIP includes the Steinberg VST3 SDK and Microsoft WebView2 loader notices. Confirm which JUCE 8 license covers the plugin distribution. [Third-party notices](../licenses/README.md) · [JUCE 8 licensing](https://juce.com/legal/juce-8-licence/)

## 0.6.0 packaging evidence

Build commit `38eb624` produced and checked the packages.

- The extracted Windows ZIP passed the VST3 host test. REAPER loaded the extracted bundle and played its piano roll with DAW sync.
- The extracted web ZIP and the live Pages build both imported MIDI and rendered and stopped audio through the worklet. The deployed panel and Wasm assets matched the local build. [GitHub Pages deployment](https://github.com/BlinkZer0/Ocarina-of-Resonance/actions/runs/44915107286)
- The debug APK and both QA-signed Android formats reported version 0.6.0 and version code 7. The packaging verifier checked ARM64 ELF segments, APK alignment, AAB alignment settings, and the APK and AAB certificates. The Samsung SM-S948U1 launched the signed QA APK and installed bundletool's universal APK over it using the same certificate. The phone ran with 4 KB pages during that test.
- The version check rejected a mismatched tag. Both Gradle release tasks rejected absent credentials, and the package verifier rejected a mismatched certificate. The temporary QA signing key was removed after verification. Production signing and the public Release require the owner's key. [GitHub packaging rehearsal](https://github.com/BlinkZer0/Ocarina-of-Resonance/actions/runs/44915105172)
