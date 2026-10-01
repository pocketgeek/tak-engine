#!/usr/bin/env bash
# Static streaming dependencies. GPU vendor runtimes remain OS/driver supplied.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${PREFIX:-$here/third_party/ffmpeg-bink}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
root="$PREFIX/stream-src"
mkdir -p "$root" "$PREFIX/include"
fetch() {
  if [ ! -d "$root/$1/.git" ]; then
    git clone --depth 1 --branch "$3" "$2" "$root/$1"
  fi
}
# Fixed source revision; no GPL-incompatible codecs. TAK is GPLv3.
if [ ! -f "$PREFIX/lib/libx264.a" ]; then
  mkdir -p "$root/x264"
  (cd "$root/x264"
   git init
   git fetch --depth 1 https://github.com/mirror/x264.git c24e06c2e184345ceb33eb20a15d1024d9fd3497
   git checkout --detach FETCH_HEAD
   args=()
   [ -n "${CROSS_PREFIX:-}" ] && args+=("--cross-prefix=$CROSS_PREFIX" "--host=${CROSS_PREFIX%-}")
   # MSYS2's shell runs as x64 even on Windows ARM64. config.guess would
   # therefore select x86 code/flags for the native CLANGARM64 compiler.
   if [ -z "${CROSS_PREFIX:-}" ] && [ "${TARGET_OS:-}" = mingw32 ] && [ -n "${TARGET_ARCH:-}" ]; then
     args+=("--host=$TARGET_ARCH-w64-mingw32")
   fi
   ./configure --prefix="$PREFIX" --enable-static --enable-pic --disable-cli --disable-opencl --disable-asm ${args[@]+"${args[@]}"}
   make -j"$JOBS"
   make install-lib-static)
fi
platform="${TARGET_OS:-$(uname -s)}"
# Multiplayer TLS needs static OpenSSL on every platform, including those
# whose FFmpeg uses the native OS TLS backend.
openssl_version=3.5.8
if [ ! -f "$PREFIX/lib/libssl.a" ] || [ "$(cat "$PREFIX/openssl-version" 2>/dev/null || true)" != "$openssl_version" ]; then
  fetch "openssl-$openssl_version" https://github.com/openssl/openssl.git "openssl-$openssl_version"
  (cd "$root/openssl-$openssl_version"
   tls_args=(--prefix="$PREFIX" --libdir=lib no-shared no-module no-tests no-apps)
   case "$platform" in
     MINGW*|MSYS*|mingw32)
       if [ "${TARGET_ARCH:-}" = aarch64 ]; then
         cat > Configurations/99-tak.conf <<'TLSCONFIG'
my %targets = (
    "tak-mingw-arm64" => {
        inherit_from => [ "mingw-common" ],
        sys_id => "MINGW64", bn_ops => "SIXTY_FOUR_BIT",
        asm_arch => "aarch64", uplink_arch => undef,
    },
);
TLSCONFIG
         perl Configure tak-mingw-arm64 no-asm "${tls_args[@]}"
       else
         perl Configure mingw64 "${tls_args[@]}"
       fi ;;
     Darwin|darwin)
       CFLAGS="${CFLAGS:-} -mmacosx-version-min=${MACOSX_DEPLOYMENT_TARGET:-14.0}" ./config "${tls_args[@]}" ;;
     *) ./config "${tls_args[@]}" ;;
   esac
   make -j"$JOBS"
   make install_sw)
  printf '%s\n' "$openssl_version" > "$PREFIX/openssl-version"
fi
case "$platform" in
  Darwin|darwin) exit 0 ;;
esac
fetch nvcodec https://github.com/FFmpeg/nv-codec-headers.git n12.1.14.1
make -C "$root/nvcodec" PREFIX="$PREFIX" install
case "$platform" in
  MINGW*|MSYS*|mingw32)
    fetch amf https://github.com/GPUOpen-LibrariesAndSDKs/AMF.git v1.4.35
    mkdir -p "$PREFIX/include/AMF"
    cp -R "$root/amf/amf/public/include/." "$PREFIX/include/AMF/"
    fetch vpl https://github.com/intel/libvpl.git v2.14.0
    # An undefined _MSC_VER is zero in MinGW. Upstream's old-MSVC fallback
    # then shadows real CRT functions and breaks newer Windows SDK headers.
    sed -i.bak 's/^#if _MSC_VER < 1400$/#if defined(_MSC_VER) \&\& _MSC_VER < 1400/' \
      "$root/vpl/libvpl/src/windows/mfx_dispatcher_defs.h"
    vpl_args=()
    if [ -n "${CROSS_PREFIX:-}" ]; then
      vpl_args+=(-DCMAKE_SYSTEM_NAME=Windows "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc" "-DCMAKE_CXX_COMPILER=${CROSS_PREFIX}g++")
    fi
    cmake -S "$root/vpl" -B "$root/vpl/build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
      -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF ${vpl_args[@]+"${vpl_args[@]}"}
    cmake --build "$root/vpl/build" -j"$JOBS"
    cmake --install "$root/vpl/build"
    ;;
  *)
    # Meson is run from pinned source; no pip install or system Meson needed.
    fetch meson https://github.com/mesonbuild/meson.git 1.7.0
    fetch drm https://gitlab.freedesktop.org/mesa/drm.git libdrm-2.4.124
    if [ ! -f "$PREFIX/lib/libdrm.a" ]; then
      python3 "$root/meson/meson.py" setup "$root/drm/build" "$root/drm" \
        --prefix="$PREFIX" --libdir=lib --default-library=static --buildtype=release \
        -Dintel=disabled -Dradeon=disabled -Damdgpu=disabled -Dnouveau=disabled \
        -Dvmwgfx=disabled -Domap=disabled -Dfreedreno=disabled -Dtegra=disabled \
        -Dvc4=disabled -Detnaviv=disabled -Dcairo-tests=disabled -Dman-pages=disabled \
        -Dtests=false
      ninja -C "$root/drm/build" install
    fi
    fetch va https://github.com/intel/libva.git 2.22.0
    # Upstream libva hardcodes shared_library(), ignoring default-library.
    # Make its loader honor the static build requested here.
    sed -i.bak 's/shared_library(/library(/g' "$root/va/va/meson.build"
    export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
    if [ ! -f "$PREFIX/lib/libva.a" ]; then
      python3 "$root/meson/meson.py" setup "$root/va/build" "$root/va" \
        --prefix="$PREFIX" --libdir=lib --default-library=static --buildtype=release \
        -Ddriverdir=/usr/lib/x86_64-linux-gnu/dri:/usr/lib/aarch64-linux-gnu/dri:/usr/lib64/dri:/usr/lib/dri \
        -Dwith_x11=no -Dwith_wayland=no -Dwith_glx=no -Ddisable_drm=false -Denable_docs=false
      ninja -C "$root/va/build" install
    fi
    ;;
esac
