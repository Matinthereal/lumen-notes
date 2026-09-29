#!/usr/bin/env bash
# Everything an Android build needs, with no root: JDK 17, the Android SDK + NDK, an x86_64 emulator
# image for testing, and Qt for Android with the matching host Qt. Re-running skips what is there.
# CI sets ANDROID_EMULATOR=0 and QT_ANDROID_ARCHS=android_arm64_v8a to fetch only what a release needs.
set -euo pipefail

QT_VERSION="${QT_VERSION:-6.10.3}"
QT_DIR="${QT_DIR:-$HOME/Qt}"
SDK="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
JDK="${JDK_DIR:-$HOME/Android/jdk-17}"
# Qt 6.10 is built and tested against NDK r27c and JDK 17 (newer JDKs break its Gradle).
NDK_VERSION=27.2.12479018
CMDLINE_TOOLS=commandlinetools-linux-16111833_latest.zip
CACHE="$HOME/Android/.downloads"
QT_ANDROID_ARCHS="${QT_ANDROID_ARCHS:-android_arm64_v8a android_x86_64}"
packages=("platform-tools" "platforms;android-35" "platforms;android-36" "build-tools;35.0.0" "ndk;$NDK_VERSION")
[ "${ANDROID_EMULATOR:-1}" = 1 ] && packages+=("emulator" "system-images;android-35;google_apis;x86_64")

mkdir -p "$SDK" "$CACHE"

if [ ! -x "$JDK/bin/java" ]; then
  curl -fL -o "$CACHE/jdk17.tar.gz" \
    https://api.adoptium.net/v3/binary/latest/17/ga/linux/x64/jdk/hotspot/normal/eclipse
  mkdir -p "$JDK"
  tar -xzf "$CACHE/jdk17.tar.gz" --strip-components=1 -C "$JDK"
fi
export JAVA_HOME="$JDK"

if [ ! -x "$SDK/cmdline-tools/latest/bin/sdkmanager" ]; then
  curl -fL -o "$CACHE/$CMDLINE_TOOLS" "https://dl.google.com/android/repository/$CMDLINE_TOOLS"
  rm -rf "$CACHE/cmdline-tools"
  unzip -q "$CACHE/$CMDLINE_TOOLS" -d "$CACHE"
  mkdir -p "$SDK/cmdline-tools"
  mv "$CACHE/cmdline-tools" "$SDK/cmdline-tools/latest"
fi
SDKMANAGER="$SDK/cmdline-tools/latest/bin/sdkmanager"

# `yes` dies of SIGPIPE once sdkmanager stops reading, which pipefail would report as a failure.
set +o pipefail
yes | "$SDKMANAGER" --sdk_root="$SDK" --licenses >/dev/null
set -o pipefail
"$SDKMANAGER" --sdk_root="$SDK" "${packages[@]}"

# Cross-building for Android needs a desktop Qt of exactly the same version for the host tools.
[ -d "$QT_DIR/$QT_VERSION/gcc_64" ] || aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$QT_DIR"
for arch in $QT_ANDROID_ARCHS; do
  [ -d "$QT_DIR/$QT_VERSION/$arch" ] || aqt install-qt all_os android "$QT_VERSION" "$arch" -O "$QT_DIR"
done

cat <<EOF
Android toolchain ready.
  JAVA_HOME=$JDK
  ANDROID_SDK_ROOT=$SDK
  ANDROID_NDK_ROOT=$SDK/ndk/$NDK_VERSION
  Qt for Android: $QT_DIR/$QT_VERSION/android_arm64_v8a (device), android_x86_64 (emulator)
EOF
