#!/bin/sh
# Builds ar_mcastlock.apk with the Android SDK command-line tools alone (no Gradle):
# aapt2 links the manifest, javac and d8 make the dex, zipalign and apksigner finish it.
#
# Usage: build_apk.sh <out_dir>
# Needs ANDROID_SDK_ROOT (or ANDROID_HOME) with a platform and build-tools installed, a JDK,
# and zip. The APK is signed with a throwaway debug key made on the spot; it is a test tool.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=$(mkdir -p "$1" && cd "$1" && pwd)
sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:?set ANDROID_SDK_ROOT}}
tools=$(ls -d "$sdk"/build-tools/* | sort -V | tail -1)
# The newest stable platform; preview and beta platforms (android-37.2-beta3 and the like) are skipped.
platform=$(ls -d "$sdk"/platforms/android-* | grep -E '/android-[0-9]+$' | sort -V | tail -1)
jar="$platform/android.jar"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
echo "build-tools: $tools"
echo "platform:    $platform"

"$tools/aapt2" link -I "$jar" --manifest "$here/AndroidManifest.xml" \
    --min-sdk-version 23 --target-sdk-version 34 -o "$work/base.apk"
mkdir -p "$work/classes" "$work/dex"
javac --release 8 -Xlint:-options -classpath "$jar" -d "$work/classes" \
    "$here/src/org/arsim/mcastlock/MainActivity.java"
"$tools/d8" --min-api 23 --lib "$jar" --output "$work/dex" $(find "$work/classes" -name '*.class')
(cd "$work/dex" && zip -q "$work/base.apk" classes.dex)
"$tools/zipalign" -f 4 "$work/base.apk" "$work/aligned.apk"
keytool -genkeypair -keystore "$work/debug.jks" -storepass android -keypass android \
    -alias ar_sim -keyalg RSA -keysize 2048 -validity 3650 -dname "CN=ar_sim harness" >/dev/null 2>&1
"$tools/apksigner" sign --ks "$work/debug.jks" --ks-pass pass:android --key-pass pass:android \
    --out "$out/ar_mcastlock.apk" "$work/aligned.apk"
"$tools/apksigner" verify "$out/ar_mcastlock.apk"
echo "wrote $out/ar_mcastlock.apk"
