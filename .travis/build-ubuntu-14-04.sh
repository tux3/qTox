#!/bin/bash
#
#    Copyright © 2015-2018 by The qTox Project Contributors
#
#    This program is libre software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

# stop as soon as one of steps will fail
set -e -o pipefail

# Qt 5.5, since that's the lowest supported version
sudo add-apt-repository -y ppa:beineri/opt-qt551-trusty
sudo apt-get update -qq

# install needed Qt, OpenAL, opus, qrencode, GTK tray deps, sqlcipher
# `--force-yes` since we don't care about GPG failing to work with short IDs
sudo apt-get install -y --force-yes \
    automake \
    autotools-dev \
    build-essential \
    check \
    checkinstall \
    libexif-dev \
    libgdk-pixbuf2.0-dev \
    libglib2.0-dev \
    libgtk2.0-dev \
    libkdeui5 \
    libopenal-dev \
    libopus-dev \
    libqrencode-dev \
    libsqlcipher-dev \
    libtool \
    libvpx-dev \
    libxss-dev qrencode \
    qt55base \
    qt55script \
    qt55svg \
    qt55tools \
    qt55xmlpatterns \
    pkg-config || yes

# Qt
source /opt/qt55/bin/qt55-env.sh || yes

# ffmpeg
if [ ! -e "libs" ]; then mkdir libs; fi
if [ ! -e "ffmpeg" ]; then mkdir ffmpeg; fi
#
cd libs/
export PREFIX_DIR="$PWD"
#
cd ../ffmpeg
wget http://ffmpeg.org/releases/ffmpeg-2.8.5.tar.bz2
tar xf ffmpeg*
cd ffmpeg*
# enabled:
# v4l2 -> webcam
# x11grab_xcb -> screen grabbing
# demuxers, decoders and parsers needed for webcams:
# mjpeg, h264

CC="ccache $CC" CXX="ccache $CXX" ./configure --prefix="$PREFIX_DIR" \
    --disable-avfilter \
    --disable-avresample \
    --disable-bzlib \
    --disable-bsfs \
    --disable-dct \
    --disable-decoders \
    --disable-demuxers \
    --disable-doc \
    --disable-dwt \
    --disable-encoders \
    --disable-faan \
    --disable-fft \
    --disable-filters \
    --disable-iconv \
    --disable-indevs \
    --disable-lsp \
    --disable-lzma \
    --disable-lzo \
    --disable-mdct \
    --disable-muxers \
    --disable-network \
    --disable-outdevs \
    --disable-parsers \
    --disable-postproc \
    --disable-programs \
    --disable-protocols \
    --disable-rdft \
    --disable-sdl \
    --disable-static \
    --disable-swresample \
    --disable-swscale-alpha \
    --disable-vaapi \
    --disable-vdpau \
    --disable-xlib \
    --disable-yasm \
    --disable-zlib \
    --enable-shared \
    --enable-memalign-hack \
    --enable-indev=v4l2 \
    --enable-indev=x11grab_xcb \
    --enable-demuxer=h264 \
    --enable-demuxer=mjpeg \
    --enable-parser=h264 \
    --enable-parser=mjpeg \
    --enable-decoder=h264 \
    --enable-decoder=mjpeg

CC="ccache $CC" CXX="ccache $CXX" make -j$(nproc)
make install
cd ../../
# libsodium
git clone git://github.com/jedisct1/libsodium.git
cd libsodium
git checkout tags/1.0.8
./autogen.sh
CC="ccache $CC" CXX="ccache $CXX" ./configure
CC="ccache $CC" CXX="ccache $CXX" make -j$(nproc)
sudo checkinstall --install --pkgname libsodium --pkgversion 1.0.8 --nodoc -y
sudo ldconfig
cd ..
# toxcore
git clone --branch v0.2.7 --depth=1 https://github.com/toktok/c-toxcore.git toxcore
cd toxcore
autoreconf -if
CC="ccache $CC" CXX="ccache $CXX" ./configure
CC="ccache $CC" CXX="ccache $CXX" make -j$(nproc) > /dev/null
sudo make install
echo '/usr/local/lib/' | sudo tee -a /etc/ld.so.conf.d/locallib.conf
sudo ldconfig
cd ..

# filteraudio
git clone --branch v0.0.1 --depth=1 https://github.com/irungentoo/filter_audio filteraudio
cd filteraudio
CC="ccache $CC" CXX="ccache $CXX" sudo make install -j$(nproc)
sudo ldconfig
cd ..

$CC --version
$CXX --version

# needed, otherwise ffmpeg doesn't get detected
export PKG_CONFIG_PATH="$PWD/libs/lib/pkgconfig"

build_qtox() {
    bdir() {
        cd $BUILDDIR
        cmake --build .
        # check if `qtox` file has been made, is non-empty and is an executable
        [[ -s qtox ]] && [[ -x qtox ]]
        # build package
        cmake --build . --target package
        cd -
    }

    local BUILDDIR=_build

    git fetch origin
    git fetch origin --tags

    # first build qTox without support for optional dependencies
    echo '*** BUILDING "MINIMAL" VERSION ***'
    cmake -H. -B"$BUILDDIR" \
        -DSMILEYS=DISABLED \
        -DENABLE_STATUSNOTIFIER=False \
        -DENABLE_GTK_SYSTRAY=False \
        -DSPELL_CHECK=OFF

    bdir

    # clean it up, and build normal version
    rm -rf "$BUILDDIR"

    echo '*** BUILDING "FULL" VERSION ***'
    cmake -H. -B"$BUILDDIR" \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_BUILD_TYPE="Release"

    bdir

    REP=qtox-nightly
    PKG=`ls _build/qtox-nightly-*-Linux.deb`

    VERSION=`git describe`
    URL_BASE="https://api.bintray.com/content/qtox/qTox/$REP/$VERSION"
    PARAMS="deb_distribution=trusty;deb_component=main;deb_architecture=amd64;publish=1"
    curl -T $PKG -udiadlo:$BINTRAY_API_KEY "$URL_BASE/$PKG;$PARAMS"
}

test_qtox() {
    local BUILDDIR=_build

    cd $BUILDDIR
    make test
    cd -
}

# CMake is supposed to process files, e.g. ones with versions.
# Check whether those changes have been committed.
check_if_differs() {
    echo "Checking whether files processed by CMake have been committed..."
    echo ""
    # ↓ `0` exit status only if there are no changes
    git diff --exit-code
}

main() {
    build_qtox
    test_qtox
    check_if_differs
}
main
