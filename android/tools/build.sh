#!/usr/bin/env bash
#
# Builds the Android app using the local toolchain when it is present, so that
# `android/.toolchain` (created by setup-toolchain.sh) does not have to download
# a second copy of Gradle through the wrapper.
#
# Falls back to the checked-in wrapper when the toolchain is absent, which is the
# path anyone with Android Studio will take.
#
#   android/tools/build.sh :app:assembleDebug
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ANDROID_DIR="$(cd "$HERE/.." && pwd)"
TOOLCHAIN="$ANDROID_DIR/.toolchain"

if [ -x "$TOOLCHAIN/gradle/bin/gradle" ]; then
    eval "$("$HERE/setup-toolchain.sh" --env)"
    # AGP reads the SDK location from local.properties or ANDROID_HOME; writing it
    # keeps the two in agreement no matter which one this AGP version prefers.
    echo "sdk.dir=$ANDROID_SDK_ROOT" > "$ANDROID_DIR/local.properties"

    # The usual debug keystore lives in ~/.android, which AGP resolves through
    # Java's user.home -- read-only in this setup. Generate an equivalent one in
    # the toolchain and hand AGP the path instead.
    KEYSTORE="$TOOLCHAIN/debug.keystore"
    if [ ! -f "$KEYSTORE" ]; then
        echo "==> Generating a local debug keystore"
        "$JAVA_HOME/bin/keytool" -genkeypair \
            -keystore "$KEYSTORE" -storetype PKCS12 \
            -storepass android -keypass android \
            -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
            -dname "CN=Android Debug,O=Android,C=US" >/dev/null
    fi

    cd "$ANDROID_DIR"
    exec "$TOOLCHAIN/gradle/bin/gradle" "$@" \
        "-PbbshdDebugKeystore=$KEYSTORE" --console=plain
fi

cd "$ANDROID_DIR"
exec ./gradlew "$@" --console=plain
