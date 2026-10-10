#!/usr/bin/env bash
# Build the XPanel release files in dist/:
#   XPanel-<version>-x86_64.AppImage        one file with everything (Qt included): run it, or
#                                           sudo ./XPanel-*.AppImage --install
#   xpanel-<version>-linux-x86_64.tar.gz    binaries + install scripts, for the system's Qt
#   SHA256SUMS
#
#   ./build-release.sh                 build with the Qt 6 installed on this system
#   ./build-release.sh --container     build inside debian:trixie (docker or podman); the
#                                      result runs on every distro with Qt >= 6.8 (Arch, CachyOS,
#                                      Fedora, Bazzite, Debian 13, ...). This is what CI uses.
#   ./build-release.sh --qt /opt/Qt/6.8.3/gcc_64   use a Qt from another prefix
#   ./build-release.sh --clean         remove the old build tree first
#   ./build-release.sh --no-appimage   only the archive (the AppImage tools are downloaded
#                                      from github.com/linuxdeploy on first use)
#
# Linux x86_64 only: the ROG Ally X is an x86_64 device and XPanel uses Linux kernel
# interfaces (sysfs, hidraw, evdev/uinput, systemd). There is no Windows or 32-bit build;
# on Windows use Armoury Crate. On Windows, build-release.bat runs this script in WSL.
#
# The AppImage bundles Qt, the Wayland/X11 plugins and layer-shell-qt, so it only needs
# glibc >= the build system's (2.41 for debian:trixie). The archive needs Qt >= 6.8 installed.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

CLEAN=0 CONTAINER=0 APPIMAGE=1 QT_PREFIX=""
while [ $# -gt 0 ]; do
    case "$1" in
        --clean) CLEAN=1 ;;
        --container) CONTAINER=1 ;;
        --no-appimage) APPIMAGE=0 ;;
        --qt) QT_PREFIX="$2"; shift ;;
        -h|--help) sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1 (see --help)"; exit 2 ;;
    esac
    shift
done

VERSION=$(sed -n 's/^project(XPanel VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "Cannot read the version from CMakeLists.txt"; exit 1; }

if [ "$CONTAINER" = 1 ]; then
    ENGINE=$(command -v podman || command -v docker || true)
    [ -n "$ENGINE" ] || { echo "--container needs podman or docker"; exit 1; }
    ARGS=""; [ "$APPIMAGE" = 0 ] && ARGS=--no-appimage
    exec "$ENGINE" run --rm -v "$PWD":/src:Z -w /src debian:trixie bash -c '
        set -e
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends $(cat packaging/build-deps-debian.txt) >/dev/null
        ./build-release.sh --clean '"$ARGS"'
        chown -R '"$(id -u):$(id -g)"' dist build-release'
fi

ARCH=$(uname -m)
if [ "$ARCH" != x86_64 ]; then
    echo "Warning: building on $ARCH. XPanel targets the ROG Ally X (x86_64); this build is untested."
fi
for t in cmake ninja; do
    command -v $t >/dev/null || { echo "Missing build tool: $t"; exit 1; }
done

BUILD=build-release
NAME="xpanel-$VERSION-linux-$ARCH"
STAGE="dist/$NAME"
[ "$CLEAN" = 1 ] && rm -rf "$BUILD"
APPIMAGE_FILE="XPanel-$VERSION-$ARCH.AppImage"
rm -rf "$STAGE" "dist/$NAME.tar.gz" "dist/$APPIMAGE_FILE" dist/SHA256SUMS dist/*.sha256

echo "==> Building XPanel $VERSION ($ARCH)"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DXPANEL_BUILD_TOOLS=OFF -DXPANEL_PORTABLE=ON \
    ${QT_PREFIX:+-DCMAKE_PREFIX_PATH="$QT_PREFIX"}
cmake --build "$BUILD"

echo "==> Checking the binaries"
"$BUILD/bin/XPanel" --version
"$BUILD/bin/xpanel-daemon" --version
if command -v objdump >/dev/null && objdump -T "$BUILD/bin/XPanel" | grep -q PRIVATE_API; then
    echo "XPanel links Qt private API: it would only run with this exact Qt version"; exit 1
fi

echo "==> Staging $STAGE"
mkdir -p "$STAGE/bin" "$STAGE/daemon" "$STAGE/qml/icons" "$STAGE/scripts" "$STAGE/packaging"
install -m 755 "$BUILD/bin/XPanel" "$BUILD/bin/xpanel-daemon" "$STAGE/bin/"
strip --strip-unneeded "$STAGE/bin/XPanel" "$STAGE/bin/xpanel-daemon" 2>/dev/null || true
install -m 644 daemon/xpanel-daemon.service "$STAGE/daemon/"
install -m 644 qml/icons/app.svg "$STAGE/qml/icons/"
install -m 755 scripts/install.sh scripts/install-daemon.sh scripts/uninstall.sh scripts/uninstall-daemon.sh "$STAGE/scripts/"
install -m 644 packaging/xpanel.desktop packaging/xpanel.sysusers "$STAGE/packaging/"
install -m 644 README.md LICENSE CHANGELOG.md "$STAGE/"
cat > "$STAGE/INSTALL.txt" <<EOF
XPanel $VERSION for Linux $ARCH (ROG Ally X, KDE Plasma 6)

Needs: Qt 6 >= 6.8 (qt6-base, qt6-declarative, qt6-svg), layer-shell-qt, a kernel with asus-armoury.

  scripts/install.sh                            # app + tray icon + autostart (your user)
  sudo scripts/install-daemon.sh --disable-hhd  # root service: TDP, fan, charge limit, lighting, input

Remove: scripts/uninstall.sh and sudo scripts/uninstall-daemon.sh
More: https://github.com/Adhamura/XPanel
EOF

echo "==> Packing"
tar -C dist --owner=0 --group=0 -czf "dist/$NAME.tar.gz" "$NAME"
rm -rf "$STAGE"

if [ "$APPIMAGE" = 1 ]; then
    echo "==> AppImage"
    TOOLS="$PWD/$BUILD/tools"
    APPDIR="$PWD/$BUILD/AppDir"
    mkdir -p "$TOOLS"
    for t in linuxdeploy/linuxdeploy linuxdeploy-plugin-qt/linuxdeploy-plugin-qt; do
        f="$TOOLS/${t#*/}-x86_64.AppImage"
        [ -x "$f" ] || { curl -fsSL -o "$f" "https://github.com/linuxdeploy/${t%/*}/releases/download/continuous/${t#*/}-x86_64.AppImage" && chmod +x "$f"; }
    done
    QMAKE=""
    for q in ${QT_PREFIX:+"$QT_PREFIX/bin/qmake6" "$QT_PREFIX/bin/qmake"} "$(command -v qmake6 || true)" /usr/lib/qt6/bin/qmake /usr/lib64/qt6/bin/qmake; do
        [ -n "$q" ] && [ -x "$q" ] && { QMAKE=$q; break; }
    done
    [ -n "$QMAKE" ] || { echo "qmake6 not found (needed to bundle Qt)"; exit 1; }
    PLUGINS=$("$QMAKE" -query QT_INSTALL_PLUGINS)
    WAYLAND=$(cd "$PLUGINS/platforms" && ls libqwayland*.so 2>/dev/null | paste -sd';' || true)
    [ -n "$WAYLAND" ] || echo "Warning: no Qt Wayland platform plugin found; the AppImage will be X11-only"

    rm -rf "$APPDIR"
    DESTDIR="$APPDIR" cmake --install "$BUILD" --prefix /usr >/dev/null
    install -Dm755 packaging/appimage/appimage-install.sh "$APPDIR/usr/share/xpanel/appimage-install.sh"
    (
        export PATH="$TOOLS:$PATH" APPIMAGE_EXTRACT_AND_RUN=1 QMAKE QML_SOURCES_PATHS="$PWD/qml"
        export EXTRA_PLATFORM_PLUGINS="$WAYLAND" EXTRA_QT_MODULES="waylandclient"
        export LDAI_OUTPUT="dist/$APPIMAGE_FILE" OUTPUT="dist/$APPIMAGE_FILE" LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
        export LDAI_NO_APPSTREAM=1 NO_APPSTREAM=1
        linuxdeploy-x86_64.AppImage --appdir "$APPDIR" \
            --executable "$APPDIR/usr/bin/XPanel" --executable "$APPDIR/usr/bin/xpanel-daemon" \
            --desktop-file "$APPDIR/usr/share/applications/xpanel.desktop" \
            --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/xpanel.svg" \
            --custom-apprun packaging/appimage/AppRun \
            --plugin qt --output appimage
    )
    # layer-shell-qt's Wayland shell plugin makes the panel an overlay; make sure it came along.
    if [ -n "$WAYLAND" ] && [ -f "$PLUGINS/wayland-shell-integration/liblayer-shell.so" ] \
       && [ ! -f "$APPDIR/usr/plugins/wayland-shell-integration/liblayer-shell.so" ]; then
        echo "AppImage is missing wayland-shell-integration/liblayer-shell.so"; exit 1
    fi
    [ -f "dist/$APPIMAGE_FILE" ] || { echo "linuxdeploy did not write dist/$APPIMAGE_FILE"; exit 1; }
    chmod +x "dist/$APPIMAGE_FILE"
    echo "==> Checking the AppImage"
    APPIMAGE_EXTRACT_AND_RUN=1 "dist/$APPIMAGE_FILE" --version
    APPIMAGE_EXTRACT_AND_RUN=1 "dist/$APPIMAGE_FILE" --daemon --version
fi

(cd dist && if [ -f "$APPIMAGE_FILE" ]; then sha256sum "$APPIMAGE_FILE" "$NAME.tar.gz"; else sha256sum "$NAME.tar.gz"; fi > SHA256SUMS)

echo
echo "Release ready:"
ls -lh dist/
