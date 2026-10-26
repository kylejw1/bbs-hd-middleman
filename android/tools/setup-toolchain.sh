#!/usr/bin/env bash
#
# Bootstraps a throwaway JDK + Android SDK + Gradle inside android/.toolchain so
# the app can be built without installing Android Studio.
#
# Nothing here is required if you already have Android Studio or a system SDK --
# this exists so the app can be built (and the APK verified) on a machine that
# has no Android tooling at all. Everything lands in android/.toolchain, which is
# gitignored; `rm -rf android/.toolchain` reclaims all of it.
#
# Usage:
#   android/tools/setup-toolchain.sh          # install (idempotent)
#   source android/tools/setup-toolchain.sh --env   # just print the env exports
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ANDROID_DIR="$(cd "$HERE/.." && pwd)"
ROOT="$ANDROID_DIR/.toolchain"
DL="$ROOT/dl"
SDK="$ROOT/android-sdk"
JDK="$ROOT/jdk"
GRADLE_HOME_DIR="$ROOT/gradle"

CMDLINE_TOOLS_ZIP="commandlinetools-linux-11076708_latest.zip"
CMDLINE_TOOLS_URL="https://dl.google.com/android/repository/$CMDLINE_TOOLS_ZIP"
GRADLE_VERSION="8.7"
GRADLE_URL="https://services.gradle.org/distributions/gradle-$GRADLE_VERSION-bin.zip"
JDK_URL="https://api.adoptium.net/v3/binary/latest/17/ga/linux/x64/jdk/hotspot/normal/eclipse?project=jdk"

# Must match compileSdk / the AGP version in app/build.gradle.kts.
PLATFORM="platforms;android-34"
BUILD_TOOLS="build-tools;34.0.0"

# Snapshot the caller's PATH before this script prepends anything to it. Without
# this, print_env appends the already-modified PATH and every `eval` of the
# output grows it by another copy of the toolchain directories.
ORIGINAL_PATH="${PATH:-/usr/local/bin:/usr/bin:/bin}"

print_env() {
    cat <<EOF
export JAVA_HOME="$JDK"
export ANDROID_SDK_ROOT="$SDK"
export ANDROID_HOME="$SDK"
export GRADLE_USER_HOME="$ROOT/gradle-home"
export HOME="$ROOT/home"
export JAVA_OPTS="-Duser.home=$ROOT/home"
export PATH="$JDK/bin:$GRADLE_HOME_DIR/bin:$SDK/platform-tools:$ORIGINAL_PATH"
EOF
}

if [ "${1:-}" = "--env" ]; then
    print_env
    exit 0
fi

mkdir -p "$DL"

fetch() { # url dest
    if [ -s "$2" ]; then
        echo "  have $(basename "$2")"
    else
        echo "  fetching $(basename "$2")"
        curl -sSL --retry 3 --retry-delay 2 -o "$2" "$1"
    fi
}

echo "==> JDK 17"
if [ ! -x "$JDK/bin/javac" ]; then
    fetch "$JDK_URL" "$DL/jdk.tar.gz"
    rm -rf "$ROOT"/jdk-17* "$JDK"
    tar -xzf "$DL/jdk.tar.gz" -C "$ROOT"
    mv "$ROOT"/jdk-17* "$JDK"
fi
echo "  $("$JDK/bin/javac" -version 2>&1)"

echo "==> Gradle $GRADLE_VERSION"
if [ ! -x "$GRADLE_HOME_DIR/bin/gradle" ]; then
    fetch "$GRADLE_URL" "$DL/gradle.zip"
    rm -rf "$GRADLE_HOME_DIR"
    unzip -q -o "$DL/gradle.zip" -d "$ROOT"
    mv "$ROOT/gradle-$GRADLE_VERSION" "$GRADLE_HOME_DIR"
fi

echo "==> Android SDK cmdline-tools"
if [ ! -x "$SDK/cmdline-tools/latest/bin/sdkmanager" ]; then
    fetch "$CMDLINE_TOOLS_URL" "$DL/cmdline-tools.zip"
    rm -rf "$SDK/cmdline-tools"
    mkdir -p "$SDK/cmdline-tools"
    unzip -q -o "$DL/cmdline-tools.zip" -d "$SDK/cmdline-tools"
    # sdkmanager insists on the directory being literally called "latest".
    mv "$SDK/cmdline-tools/cmdline-tools" "$SDK/cmdline-tools/latest"
fi

export JAVA_HOME="$JDK"
export ANDROID_SDK_ROOT="$SDK"
export ANDROID_HOME="$SDK"
# sdkmanager resolves its download cache through Java's user.home rather than
# $HOME, so pointing only HOME at a writable directory is not enough: it still
# tries to write ~/.android/cache and reports "IO exception while downloading
# manifest" until user.home is redirected too.
export HOME="$ROOT/home"
export JAVA_OPTS="-Duser.home=$HOME"
mkdir -p "$HOME"
export PATH="$JDK/bin:$SDK/cmdline-tools/latest/bin:$SDK/platform-tools:$PATH"

# sdks are installed via the commandline tools; the licence prompt is the only
# interactive part of the whole build, so answer it here.
# Only touch the network when something is actually missing, so a second run is
# a genuine no-op and works offline. This is the difference between "the
# toolchain is already here" and "reinstall it every time".
if [ -d "$SDK/platforms/android-34" ] && [ -d "$SDK/build-tools/34.0.0" ] \
   && [ -x "$SDK/platform-tools/adb" ]; then
    echo "   SDK packages already installed"
else
    echo "==> SDK licences"
    yes 2>/dev/null | sdkmanager --sdk_root="$SDK" --licenses >/dev/null || true

    echo "==> SDK packages"
    sdkmanager --sdk_root="$SDK" --install "$PLATFORM" "$BUILD_TOOLS" "platform-tools" >/dev/null
fi

echo
echo "Toolchain ready. Use it with:"
echo "    eval \"\$($HERE/setup-toolchain.sh --env)\""
echo "    cd $ANDROID_DIR && ./gradlew :app:assembleDebug"
echo
echo "Gradle home: $ROOT/gradle-home (caches the AGP/Kotlin downloads)"
print_env
