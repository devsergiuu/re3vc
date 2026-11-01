#!/bin/sh
# reVC build script for Parrot OS / Debian (no sudo required)
# Reproduces: local prefix deps -> shader gen -> cmake build
set -e
REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPS="$REPO_DIR/.build-deps"
BUILD="$REPO_DIR/build"
TMPD="$(mktemp -d)"

mkdir -p "$DEPS" "$BUILD"

# 1) Download everything needed via apt-get download (no root)
cd "$TMPD"
# cmake binary + runtime deps
for p in cmake cmake-data librhash1 libuv1t64 libarchive13t64 libjsoncpp26 \
    libcurl4t64 libnghttp2-14 libssh2-1t64 libldap2 librtmp1 libpsl5t64 \
    libgssapi-krb5-2 libkrb5-3 libk5crypto3 libcom-err2 libidn2-0 \
    libunistring5 libbrotli1 libpsl5; do
    apt-get download "$p" 2>/dev/null | tail -0 || true
done
# game deps
for p in libopenal-dev libmpg123-dev libglfw3-dev libglfw3 libgl-dev \
    libglew-dev libglew2.2 libx11-dev libxrandr-dev libxcursor-dev \
    libxinerama-dev libxi-dev libxext-dev libxrender-dev libxfixes-dev \
    libxkbcommon-dev libxkbcommon-x11-dev libwayland-dev; do
    apt-get download "$p" 2>/dev/null | tail -0 || true
done
for f in ./*.deb; do dpkg-deb -x "$f" "$DEPS"; done
rm -rf "$TMPD"

LIBDIR="$DEPS/usr/lib/x86_64-linux-gnu"

# 2) Fix dangling dev-symlinks with real runtime libs from the system
resolve_link() { # name -> real file chain from /lib
    src=/lib/x86_64-linux-gnu/"$2"
    test -f "$src" || { echo "WARN: $src not found on system (install the runtime lib manually)"; return 0; }
    cp "$src" "$LIBDIR/"
    base="$1"
    ln -sf "$2" "$LIBDIR/$base.0" 2>/dev/null || true
}
cp -f /lib/x86_64-linux-gnu/libopenal.so.* "$LIBDIR/" 2>/dev/null || true
cp -f /lib/x86_64-linux-gnu/libmpg123.so.* "$LIBDIR/" 2>/dev/null || true
ln -sf libopenal.so.1.24.2 "$LIBDIR/libopenal.so.1"
ln -sf libopenal.so.1 "$LIBDIR/libopenal.so"
ln -sf libmpg123.so.0.48.3 "$LIBDIR/libmpg123.so.0"
ln -sf libmpg123.so.0 "$LIBDIR/libmpg123.so"
if [ ! -f /lib/x86_64-linux-gnu/libX11.so ] && [ -f /lib/x86_64-linux-gnu/libX11.so.6 ]; then
    cp /lib/x86_64-linux-gnu/libX11.so.6* "$LIBDIR/"
    ln -sf libX11.so.6.4.0 "$LIBDIR/libX11.so.6"
    ln -sf libX11.so.6 "$LIBDIR/libX11.so"
fi

# 3) Generate shader .inc files (repo ships without them)
cd "$REPO_DIR/src/extras/shaders"
mkdir -p obj
for s in *.frag *.vert; do sh makeinc_glsl.sh "$s"; done

# 4) librw submodule (fork misses it) - pin compatible revision
if [ ! -f "$REPO_DIR/vendor/librw/CMakeLists.txt" ]; then
    git clone https://github.com/aap/librw.git "$REPO_DIR/vendor/librw"
fi
cd "$REPO_DIR/vendor/librw"
git fetch --all -q 2>/dev/null || git fetch -q || true
git checkout -q 0151d72140a04f1a7e42819b7649f4c035c84ad0   # Feb 27 2021 - compatible with this reVC snapshot

# 5) Configure + build
export LD_LIBRARY_PATH="$LIBDIR"
cd "$BUILD"
"$DEPS/usr/bin/cmake" .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$DEPS/usr" \
    -DLIBRW_PLATFORM=GL3 \
    -DLIBRW_GL3_GFXLIB=GLFW \
    -DREVC_INSTALL=ON
"$DEPS/usr/bin/cmake" --build . -j"$(nproc)"

echo
echo "=== DONE: $BUILD/src/reVC ==="
echo "Runtime: LD_LIBRARY_PATH=$LIBDIR ./src/reVC"
