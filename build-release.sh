#!/usr/bin/env bash
# Build an XPanel release archive: dist/xpanel-<version>-linux-<arch>.tar.gz (+ .sha256)
#
#   ./build-release.sh                 build with the Qt 6 installed on this system
#   ./build-release.sh --container     build inside debian:trixie (docker or podman); the
#                                      result runs on every distro with Qt >= 6.8 (Arch, CachyOS,
#                                      Fedora, Bazzite, Debian 13, ...). This is what CI uses.
#   ./build-release.sh --qt /opt/Qt/6.8.3/gcc_64   use a Qt from another prefix
#   ./build-release.sh --clean         remove the old build tree first
#
# Linux x86_64 only: the ROG Ally X is an x86_64 device and XPanel uses Linux kernel
# interfaces (sysfs, hidraw, evdev/uinput, systemd). There is no Windows or 32-bit build;
# on Windows use Armoury Crate. On Windows, build-release.bat runs this script in WSL.
#
# The archive contains bin/XPanel, bin/xpanel-daemon, the systemd unit and the install
# scripts; users unpack it and run scripts/install.sh and sudo scripts/install-daemon.sh.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

CLEAN=0 CONTAINER=0 QT_PREFIX=""
while [ $# -gt 0 ]; do
    case "$1" in
        --clean) CLEAN=1 ;;
        --container) CONTAINER=1 ;;
        --qt) QT_PREFIX="$2"; shift ;;
        -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1 (see --help)"; exit 2 ;;
    esac
    shift
done

VERSION=$(sed -n 's/^project(XPanel VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "Cannot read the version from CMakeLists.txt"; exit 1; }

if [ "$CONTAINER" = 1 ]; then
    ENGINE=$(command -v podman || command -v docker || true)
    [ -n "$ENGINE" ] || { echo "--container needs podman or docker"; exit 1; }
    exec "$ENGINE" run --rm -v "$PWD":/src:Z -w /src debian:trixie bash -c '
        set -e
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
            ca-certificates cmake ninja-build g++ file \
            qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools qt6-svg-dev \
            liblayershellqtinterface-dev libgl-dev libxkbcommon-dev >/dev/null
        ./build-release.sh --clean
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
rm -rf "$STAGE" "dist/$NAME.tar.gz" "dist/$NAME.tar.gz.sha256"

echo "==> Building XPanel $VERSION ($ARCH)"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DXPANEL_BUILD_TOOLS=OFF \
    ${QT_PREFIX:+-DCMAKE_PREFIX_PATH="$QT_PREFIX"}
cmake --build "$BUILD"

echo "==> Checking the binaries"
"$BUILD/bin/XPanel" --version
"$BUILD/bin/xpanel-daemon" --version

echo "==> Staging $STAGE"
mkdir -p "$STAGE/bin" "$STAGE/daemon" "$STAGE/qml/icons" "$STAGE/scripts" "$STAGE/packaging"
install -m 755 "$BUILD/bin/XPanel" "$BUILD/bin/xpanel-daemon" "$STAGE/bin/"
strip --strip-unneeded "$STAGE/bin/XPanel" "$STAGE/bin/xpanel-daemon" 2>/dev/null || true
install -m 644 daemon/xpanel-daemon.service "$STAGE/daemon/"
install -m 644 qml/icons/app.svg "$STAGE/qml/icons/"
install -m 755 scripts/install.sh scripts/install-daemon.sh scripts/uninstall.sh scripts/uninstall-daemon.sh "$STAGE/scripts/"
install -m 644 packaging/xpanel.desktop "$STAGE/packaging/"
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
(cd dist && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
rm -rf "$STAGE"

echo
echo "Release ready:"
ls -lh "dist/$NAME.tar.gz" "dist/$NAME.tar.gz.sha256"
