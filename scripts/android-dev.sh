#!/usr/bin/env bash
# Build the Android app for the x86_64 emulator, sign it with the debug key and start it on the
# running emulator or device. Extra arguments go to the app, e.g.  scripts/android-dev.sh --uitest
# Needs scripts/setup-android.sh first.
set -euo pipefail
cd "$(dirname "$0")/.."
ABI="${ABI:-x86_64}"
BUILD="build-android-$ABI"
export JAVA_HOME="${JAVA_HOME_17:-$HOME/Android/jdk-17}"
export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
QT="${QT_DIR:-$HOME/Qt}/${QT_VERSION:-6.10.3}"
ADB="$ANDROID_SDK_ROOT/platform-tools/adb"
BT="$ANDROID_SDK_ROOT/build-tools/35.0.0"
APP_ID=io.github.matinthereal.lumen

qtabi=$([ "$ABI" = arm64-v8a ] && echo android_arm64_v8a || echo android_x86_64)
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
  "$QT/$qtabi/bin/qt-cmake" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DQT_HOST_PATH="$QT/gcc_64" -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    -DANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/27.2.12479018" -DLUMEN_UITEST_TOUCH=ON
fi
cmake --build "$BUILD" --parallel
# Gradle leaves a daemon holding ~1 GB; this laptop needs it back for the emulator.
"$BUILD/app/android-build/gradlew" -p "$BUILD/app/android-build" --stop >/dev/null 2>&1 || true

unsigned="$BUILD/app/android-build/build/outputs/apk/release/android-build-release-unsigned.apk"
apk="$BUILD/lumen-debug.apk"
"$BT/zipalign" -f -p 4 "$unsigned" "$BUILD/aligned.apk"
[ -f "$HOME/.android/debug.keystore" ] || keytool -genkeypair -keystore "$HOME/.android/debug.keystore" \
  -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
  -dname "CN=Android Debug,O=Android,C=US"
"$BT/apksigner" sign --ks "$HOME/.android/debug.keystore" --ks-pass pass:android --key-pass pass:android \
  --ks-key-alias androiddebugkey --out "$apk" "$BUILD/aligned.apk" 2>/dev/null
"$ADB" install -r "$apk"
"$ADB" shell am force-stop "$APP_ID"
"$ADB" shell am start -n "$APP_ID/org.qtproject.qt.android.bindings.QtActivity" \
  ${1:+-e applicationArguments "'$*'"}
