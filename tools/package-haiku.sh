#!/usr/bin/env bash
# Builds the Haiku package (.hpkg) on Haiku. Run from the repository root.
set -euo pipefail
AMP_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$AMP_ROOT"
NAME=$(awk '$1 == "name" { print $2; exit }' resources/Amp.PackageInfo)
VERSION=$(awk '$1 == "version" { print $2; exit }' resources/Amp.PackageInfo)
ARCH=$(awk '$1 == "architecture" { print $2; exit }' resources/Amp.PackageInfo)
FILE="$AMP_ROOT/artifacts/$NAME-$VERSION-$ARCH.hpkg"
make -j6
STAGE=$(mktemp -d /tmp/amp-package-XXXXXX)
trap 'rm -rf -- "$STAGE"' EXIT
mkdir -p "$STAGE/apps" "$STAGE/documentation/packages/amp/vendor/nlohmann" \
    "$STAGE/documentation/packages/amp/vendor/fontawesome" "$STAGE/data/Amp" \
    "$STAGE/data/licenses" "$STAGE/data/deskbar/menu/Applications" "$STAGE/boot/post-install" "$AMP_ROOT/artifacts"
cp build-haiku/Amp "$STAGE/apps/Amp"
strip --strip-debug "$STAGE/apps/Amp"
# GNU strip removes the appended Haiku resources; restore them.
xres -o "$STAGE/apps/Amp" build-haiku/Amp.rsrc
# The Quick Look add-on, where the SDK had Quick Look.
if [ -f build-haiku/Amp_quicklook ]; then
    mkdir -p "$STAGE/add-ons/quick_look"
    cp build-haiku/Amp_quicklook "$STAGE/add-ons/quick_look/Amp"
    strip --strip-debug "$STAGE/add-ons/quick_look/Amp"
fi
cp resources/Amp.PackageInfo "$STAGE/.PackageInfo"
cp README.md LICENSE "$STAGE/documentation/packages/amp/"
cp -R docs "$STAGE/documentation/packages/amp/"
cp vendor/nlohmann/LICENSE.MIT vendor/nlohmann/UPSTREAM.md "$STAGE/documentation/packages/amp/vendor/nlohmann/"
cp vendor/fontawesome/LICENSE.txt vendor/fontawesome/UPSTREAM.md "$STAGE/documentation/packages/amp/vendor/fontawesome/"
# each license named in .PackageInfo needs its text inside the package
cp vendor/fontawesome/SIL-OFL-1.1.txt "$STAGE/data/licenses/SIL OFL 1.1"
cp vendor/fontawesome/CC-BY-4.0.txt "$STAGE/data/licenses/CC BY 4.0"
# The icon font lives in the package; the app copies it into the user font directory.
cp vendor/fontawesome/FontAwesome6Free-Solid-900.otf "$STAGE/data/Amp/"
cp resources/amp-post-install.sh "$STAGE/boot/post-install/amp.sh"
ln -s ../../../../apps/Amp "$STAGE/data/deskbar/menu/Applications/Amp"
( cd "$STAGE" && mimeset --all -f --mimedb data/mime_db --mimedb /boot/system/data/mime_db apps/Amp )
package create -C "$STAGE" "$FILE"
printf '%s\n' "$FILE"
