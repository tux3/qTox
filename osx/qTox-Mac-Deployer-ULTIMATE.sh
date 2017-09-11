#!/usr/bin/env bash

#
#    Copyright © 2015 by RowenStipe
#    Copyright © 2016-2017 by The qTox Project Contributors
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

# This uses the same process as doing it manually but with a few varients

# Use: ./qTox-Mac-Deployer-ULTIMATE.sh -h

set -e

# Your home DIR really (Most of this happens in it) {DONT USE: ~ }
SUBGIT="" #Change this to define a 'sub' git folder e.g. "-Patch"
          #Applies to $QTOX_DIR, $BUILD_DIR, and $DEPLOY_DIR folders for organization puropses

if [[ $TRAVIS = true ]]
then
    MAIN_DIR="${TRAVIS_BUILD_DIR}"
    QTOX_DIR="${MAIN_DIR}"
else
    MAIN_DIR="/Users/${USER}"
    QTOX_DIR="${MAIN_DIR}/qTox${SUBGIT}"
fi
QT_DIR="/usr/local/Cellar/qt5" # Folder name of QT install
# Figure out latest version
QT_VER=($(ls ${QT_DIR} | sed -n -e 's/^\([0-9]*\.([0-9]*\.([0-9]*\).*/\1/' -e '1p;$p'))
QT_DIR_VER="${QT_DIR}/${QT_VER[1]}"

TOXCORE_DIR="${MAIN_DIR}/toxcore" # Change to Git location
FILTERAUIO_DIR="${MAIN_DIR}/filter_audio" # Change to Git location

LIB_INSTALL_PREFIX="${QTOX_DIR}/libs"

[[ ! -e "${LIB_INSTALL_PREFIX}" ]] \
&& mkdir -p "${LIB_INSTALL_PREFIX}"

BUILD_DIR="${MAIN_DIR}/qTox-Mac_Build${SUBGIT}"
DEPLOY_DIR="${MAIN_DIR}/qTox-Mac_Deployed${SUBGIT}"

# helper function to "pretty-print"
fcho() {
    local msg="$1"; shift
    printf "\n$msg\n" "$@"
}

build_toxcore() {
    echo "Starting Toxcore build and install"
    cd $TOXCORE_DIR
    echo "Now working in: ${PWD}"

    local LS_DIR="/usr/local/Cellar/libsodium/"
    #Figure out latest version
    local LS_VER=($(ls ${LS_DIR} | sed -n -e 's/^\([0-9]*\.([0-9]*\.([0-9]*\).*/\1/' -e '1p;$p'))
    local LS_DIR_VER="${LS_DIR}/${LS_VER[1]}"

    [[ $TRAVIS != true ]] \
    && sleep 3

    autoreconf -if

    #Make sure the correct version of libsodium is used
    ./configure --with-libsodium-headers="${LS_DIR_VER}/include/" --with-libsodium-libs="${LS_DIR_VER}/lib/" --prefix="${LIB_INSTALL_PREFIX}"

    make clean &> /dev/null
    fcho "Compiling toxcore."
    make > /dev/null || exit 1
    fcho "Installing toxcore."
    make install > /dev/null || exit 1
}

install() {
    fcho "=============================="
    fcho "This script will install the necessary applications and libraries needed to compile qTox properly."
    fcho "Note that this is not a 100 percent automated install it just helps simplify the process for less experienced or lazy users."
    if [[ $TRAVIS = true ]]
    then
        echo "Oh... It's just Travis...."
    else
        read -n1 -rsp $'Press any key to continue or Ctrl+C to exit...\n'
    fi

    #fcho "Installing x-code Command line tools ..."
    #xcode-select --install

    if [[ -e /usr/local/bin/brew ]]
    then
        fcho "Homebrew already installed!"
    else
        fcho "Installing homebrew ..."
        ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
    fi
    if [[ $TRAVIS != true ]]
    then
        fcho "Updating brew formulas ..."
        brew update
    fi
    fcho "Getting home brew formulas ..."
    if [[ $TRAVIS != true ]]
    then
        sleep 3
        brew install git wget libtool autoconf automake pkgconfig
    fi
    brew install check libvpx opus libsodium

    fcho "Starting git repo checks ..."

    #cd $MAIN_DIR # just in case
    # Toxcore
    if [[ -e $TOX_DIR/.git/index ]]
    then
        fcho "Toxcore git repo already in place !"
        cd $TOX_DIR
        git pull
    else
        fcho "Cloning Toxcore git ... "
        git clone --branch v0.1.0 --depth=1 https://github.com/toktok/c-toxcore "$TOXCORE_DIR"
    fi
    # qTox
    if [[ $TRAVIS = true ]]
    then
        fcho "Travis... You already have qTox..."
    else
        if [[ -e $QTOX_DIR/.git/index ]]
        then
            fcho "qTox git repo already in place !"
            cd $QTOX_DIR
            git pull
        else
            fcho "Cloning qTox git ... "
            git clone https://github.com/qTox/qTox.git
        fi
    fi

    # toxcore build
    if [[ $TRAVIS = true ]]
    then
        build_toxcore
    else
        fcho "If all went well you should now have all the tools needed to compile qTox!"
        read -r -p "Would you like to install toxcore now? [y/N] " response
        if [[ $response =~ ^([yY][eE][sS]|[yY])$ ]]
        then
            build_toxcore
        else
            fcho "You can simply use the -u command and say [Yes/n] when prompted"
        fi
    fi

    if [[ $TRAVIS = true ]]
    then
        fcho "Updating brew formulas ..."
        brew update > /dev/null
    else
        brew install cmake
    fi
    brew install ffmpeg libexif qrencode qt5 sqlcipher openal-soft

    fcho "Cloning filter_audio ... "
    git clone --branch v0.0.1 --depth=1 https://github.com/irungentoo/filter_audio "$FILTERAUIO_DIR"
    cd "$FILTERAUIO_DIR"
    fcho "Installing filter_audio ... "
    make install PREFIX="$LIB_INSTALL_PREFIX"

    QT_VER=($(ls ${QT_DIR} | sed -n -e 's/^\([0-9]*\.([0-9]*\.([0-9]*\).*/\1/' -e '1p;$p'))
    QT_DIR_VER="${QT_DIR}/${QT_VER[1]}"

    # put required by qTox libs/headers in `libs/`
    cd "${QTOX_DIR}"
    sudo ./bootstrap-osx.sh
}

update() {
    fcho "------------------------------"
    fcho "Starting update process ..."
    #First update Toxcore from git
    cd $TOXCORE_DIR
    fcho "Now in ${PWD}"
    fcho "Pulling ..."
    # make sure that pull can be applied, i.e. clean up files from any
    # changes that could have been applied to them
    git checkout -f
    git pull
    read -r -p "Did Toxcore update from git? [y/N] " response
    if [[ $response =~ ^([yY][eE][sS]|[yY])$ ]]
    then
        build_toxcore
    else
        fcho "Moving on!"
    fi

    #Now let's update qTox!
    cd $QTOX_DIR
    fcho "Now in ${PWD}"
    fcho "Pulling ..."
    # make sure that pull can be applied, i.e. clean up files from any
    # changes that could have been applied to them
    git checkout -f
    git pull
    read -r -p "Did qTox update from git? [y/N] " response
    if [[ $response =~ ^([yY][eE][sS]|[yY])$ ]]
    then
        fcho "Starting OSX bootstrap ..."
        sudo ./bootstrap-osx.sh
    else
        fcho "Moving on!"
    fi
}

build() {
    fcho "------------------------------"
    fcho "Starting build process ..."
    rm -rf $BUILD_DIR
    rm -rf $DEPLOY_DIR
    mkdir $BUILD_DIR
    cd $BUILD_DIR
    fcho "Now working in ${PWD}"
    fcho "Starting cmake ..."
    export CMAKE_PREFIX_PATH=$(brew --prefix qt5)
    cmake -H$QTOX_DIR -B.
    make -j$(sysctl -n hw.ncpu)
}

deploy() {
    fcho "------------------------------"
    fcho "starting deployment process ..."
    cd $BUILD_DIR
    if [ ! -d $BUILD_DIR ]
    then
        fcho "Error: Build directory not detected, please run -ubd, or -b before deploying"
        exit 0
    fi
    mkdir $DEPLOY_DIR
    make install
    cp -r $BUILD_DIR/qTox.app $DEPLOY_DIR/qTox.app
}

bootstrap() {
    fcho "------------------------------"
    fcho "starting bootstrap process ..."

    #Toxcore
    build_toxcore

    #Boot Strap
    fcho "Running: sudo ${QTOX_DIR_VER}/bootstrap-osx.sh"
    cd $QTOX_DIR
    sudo ./bootstrap-osx.sh
}

dmgmake() {
    fcho "------------------------------"
    fcho "Starting DMG creation"
    cp $BUILD_DIR/qTox.dmg $QTOX_DIR/
}

helpme() {
    echo "This script was created to help ease the process of compiling and creating a distributable qTox package for OSX systems."
    echo "The available commands are:"
    echo "-h  | --help      -- This help text."
    echo "-i  | --install   -- A slightly automated process for getting an OSX machine ready to build Toxcore and qTox."
    echo "-u  | --update    -- Check for updates and build Toxcore from git & update qTox from git."
    echo "-b  | --build     -- Builds qTox in: ${BUILD_DIR}"
    echo "-d  | --deploy    -- Makes a distributable qTox.app file in: ${DEPLOY_DIR}"
    echo "-bs | --bootstrap -- Performs bootstrap steps."
    fcho "Issues with Toxcore or qTox should be reported to their respective repos: https://github.com/toktok/c-toxcore | https://github.com/qTox/qTox"
    exit 0
}

case "$1" in
    -h | --help)
    helpme
    exit
    ;;
    -i | --install)
    install
    exit
    ;;
    -u | --update)
    update
    exit
    ;;
    -b | --build)
    build
    exit
    ;;
    -d | --deploy)
    deploy
    exit
    ;;
    -bs | --bootstrap)
    bootstrap
    exit
    ;;
    -dmg)
    dmgmake
    exit
    ;;
    *)
    ;;
esac

fcho "Oh dear! You seemed to of started this script improperly! Use -h to get available commands and information!"
echo " "
say -v Kathy -r 255 "Oh dear! You seemed to of started this script improperly! Use -h to get available commands and information!"
exit 0
