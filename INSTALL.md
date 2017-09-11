# Install Instructions
- [Dependencies](#dependencies)
- [Linux](#linux)
  - [Simple install](#simple-install)
    - [Arch](#arch-easy)
    - [Gentoo](#gentoo-easy)
    - [Slackware](#slackware-easy)
    - [FreeBSD](#freebsd-easy)
  - [Install git](#install-git)
    - [Arch](#arch-git)
    - [Debian](#debian-git)
    - [Fedora](#fedora-git)
    - [openSUSE](#opensuse-git)
    - [Ubuntu](#ubuntu-git)
  - [Clone qTox](#clone-qtox)
  - [GCC, Qt, FFmpeg, OpenAL Soft and qrencode](#other-deps)
    - [Arch](#arch-other-deps)
    - [Debian](#debian-other-deps)
    - [Fedora](#fedora-other-deps)
    - [openSUSE](#opensuse-other-deps)
    - [Slackware](#slackware-other-deps)
    - [Ubuntu >=15.04](#ubuntu-other-deps)
    - [Ubuntu >=16.04](#ubuntu-other-1604-deps)
  - [toxcore dependencies](#toxcore-dependencies)
    - [Arch](#arch-toxcore)
    - [Debian](#debian-toxcore)
    - [Fedora](#fedora-toxcore)
    - [openSUSE](#opensuse-toxcore)
    - [Slackware](#slackware-toxcore)
    - [Ubuntu >=15.04](#ubuntu-toxcore)
  - [sqlcipher](#sqlcipher)
  - [Compile toxcore](#compile-toxcore)
  - [Compile qTox](#compile-qtox)
- [OS X](#osx)
- [Windows](#windows)
  - [Cross-compile from Linux](#cross-compile-from-linux)
  - [Native](#native)
- [Compile-time switches](#compile-time-switches)

## Dependencies

| Name          | Version     | Modules                                      |
|---------------|-------------|----------------------------------------------|
| [Qt]          | >= 5.5.0    | core, gui, network, opengl, svg, widget, xml |
| [GCC]/[MinGW] | >= 4.8      | C++11 enabled                                |
| [toxcore]     | = 0.1.\*    | core, av                                     |
| [FFmpeg]      | >= 2.6.0    | avformat, avdevice, avcodec, avutil, swscale |
| [CMake]       | >= 2.8.11   |                                              |
| [OpenAL Soft] | >= 1.16.0   |                                              |
| [qrencode]    | >= 3.0.3    |                                              |
| [sqlcipher]   | >= 3.2.0    |                                              |
| [pkg-config]  | >= 0.28     |                                              |
| [filteraudio] | >= 0.0.1    | optional dependency                          |

## Optional dependencies

They can be disabled/enabled by passing arguments to `cmake` command when
building qTox.

If they are missing, qTox is built without support for the functionality.

### Development dependencies

Dependencies needed to run tests / code formatting, etc. Disabled if
dependencies are missing.

| Name    | Version |
|---------|---------|
| [Check] | >= 0.9  |

### Linux

#### Auto-away support

| Name            | Version  |
|-----------------|----------|
| [libXScrnSaver] | >= 1.2   |
| [libX11]        | >= 1.6.0 |

Disabled if dependencies are missing during compilation.

#### KDE Status Notifier / GTK tray backend

| Name        | Version |
|-------------|---------|
| [Atk]       | >= 2.14 |
| [Cairo]     |         |
| [GdkPixbuf] | >= 2.31 |
| [GLib]      | >= 2.0  |
| [GTK+]      | >= 2.0  |
| [Pango]     | >= 1.18 |

To disable: `-DENABLE_STATUSNOTIFIER=False -DENABLE_GTK_SYSTRAY=False`

#### Unity tray backend

Disabled by default.

| Name              | Version   |
|-------------------|-----------|
| [Atk]             | >= 2.14   |
| [Cairo]           |           |
| [DBus Menu]       | >= 0.6    |
| [GdkPixbuf]       | >= 2.31   |
| [GLib]            | >= 2.0    |
| [GTK+]            | >= 2.0    |
| [libappindicator] | >= 0.4.92 |
| [Pango]           | >= 1.18   |

To enable: `-DENABLE_APPINDICATOR=True`


## Linux
### Simple install

Easy qTox install is provided for variety of distributions:

* [Arch](#arch)
* [Gentoo](#gentoo)
* [Slackware](#slackware)

---

<a name="arch-easy" />

#### Arch

PKGBUILD is available in the `community` repo, to install:

```bash
pacman -S qtox
```

<a name="gentoo-easy" />

#### Gentoo

qTox is available in Gentoo.

To install:

```bash
emerge qtox
```


<a name="slackware-easy" />

#### Slackware

qTox SlackBuild and all of its dependencies can be found here:
http://slackbuilds.org/repository/14.2/network/qTox/

<a name="freebsd-easy" />

#### FreeBSD

qTox is available as a binary package. To install the qTox package:

```bash
pkg install qTox
```

The qTox port is also available at ``net-im/qTox``. To build and install qTox
from sources using the port:

```bash
cd /usr/ports/net-im/qTox
make install clean
```

----

If your distribution is not listed, or you want / need to compile qTox, there
are provided instructions.


----

Most of the dependencies should be available through your package manager. You
may either follow the directions below, or simply run `./simple_make.sh` after
cloning this repository, which will attempt to automatically download
dependencies followed by compilation.

### Install git
In order to clone the qTox repository you need Git.


<a name="arch-git" />

#### Arch Linux

```bash
sudo pacman -S --needed git
```

<a name="debian-git" />

#### Debian

```bash
sudo apt-get install git
```

<a name="fedora-git" />

#### Fedora

```bash
sudo dnf install git
```

<a name="opensuse-git" />

#### openSUSE

```bash
sudo zypper install git
```

<a name="ubuntu-git" />

#### Ubuntu

```bash
sudo apt-get install git
```


### Clone qTox

Afterwards open a new terminal, change to a directory of your choice and clone
the repository:

```bash
cd /home/$USER/qTox
git clone https://github.com/qTox/qTox.git qTox
```

The following steps assumes that you cloned the repository at
`/home/$USER/qTox`.  If you decided to choose another location, replace
corresponding parts.


<a name="other-deps" />

### GCC, Qt, FFmpeg, OpenAL Soft and qrencode

<a name="arch-other-deps" />

#### Arch Linux

```bash
sudo pacman -S --needed base-devel qt5 openal libxss qrencode ffmpeg
```


<a name="debian-other-deps" />

#### Debian

**Note that only Debian >=9 stable (stretch) is supported.**

```bash
sudo apt-get install \
    build-essential \
    cmake \
    ffmpeg \
    libexif-dev \
    libgdk-pixbuf2.0-dev \
    libglib2.0-dev \
    libgtk2.0-dev \
    libopenal-dev \
    libqrencode-dev \
    libqt5opengl5-dev \
    libqt5svg5-dev \
    libsqlcipher-dev \
    libxss-dev \
    pkg-config \
    qrencode \
    qt5-default \
    qt5-qmake \
    qttools5-dev \
    qttools5-dev-tools \
    yasm
```

<a name="fedora-other-deps" />

#### Fedora

**Note that sqlcipher is not included in all versions of Fedora yet.**
As of writing this section (November 2016), Fedora 25 ships sqlcipher, but
Fedora 24 and older don't ship it yet.
**This means that if you can't install sqlcipher from repositories, you'll
have to compile it yourself, otherwise compiling qTox will fail.**

```bash
sudo dnf groupinstall "Development Tools" "C Development Tools and Libraries"
# (can also use sudo dnf install @"Development Tools")
sudo dnf install \
    ffmpeg-devel \
    gtk2-devel \
    libexif-devel \
    libXScrnSaver-devel \
    libtool \
    openal-soft-devel \
    openssl-devel \
    qrencode-devel \
    qt-creator \
    qt-devel \
    qt-doc \
    qt5-linguist \
    qt5-qtsvg \
    qt5-qtsvg-devel \
    qtsingleapplication \
    sqlcipher \
    sqlcipher-devel
```

**Go to [sqlcipher](#sqlcipher) section to compile it if necessary.**

<a name="opensuse-other-deps" />

#### openSUSE

```bash
sudo zypper install \
    libexif-devel \
    libQt5Concurrent-devel \
    libQt5Network-devel \
    libQt5OpenGL-devel \
    libQt5Xml-devel \
    libXScrnSaver-devel \
    libffmpeg-devel \
    libqt5-linguist \
    libqt5-qtbase-common-devel \
    libqt5-qtsvg-devel \
    openal-soft-devel \
    patterns-openSUSE-devel_basis \
    qrencode-devel \
    sqlcipher-devel
```

<a name="slackware-other-deps" />

#### Slackware

List of all the qTox dependencies and their SlackBuilds can be found here:
http://slackbuilds.org/repository/14.2/network/qTox/


<a name="ubuntu-other-deps" />

#### Ubuntu >=15.04

```bash
sudo apt-get install \
    build-essential cmake \
    libavcodec-ffmpeg-dev \
    libavdevice-ffmpeg-dev \
    libavfilter-ffmpeg-dev \
    libavutil-ffmpeg-dev \
    libexif-dev \
    libgdk-pixbuf2.0-dev \
    libglib2.0-dev \
    libgtk2.0-dev \
    libopenal-dev \
    libqrencode-dev \
    libqt5opengl5-dev \
    libqt5svg5-dev \
    libsqlcipher-dev \
    libswresample-ffmpeg-dev \
    libswscale-ffmpeg-dev \
    libxss-dev \
    qrencode \
    qt5-default \
    qt5-qmake \
    qttools5-dev-tools
```

<a name="ubuntu-other-1604-deps" />

#### Ubuntu >=16.04:

```bash
sudo apt-get install \
    build-essential \
    cmake \
    libavcodec-dev \
    libavdevice-dev \
    libavfilter-dev \
    libavutil-dev \
    libexif-dev \
    libgdk-pixbuf2.0-dev \
    libglib2.0-dev \
    libgtk2.0-dev \
    libopenal-dev \
    libqrencode-dev \
    libqt5opengl5-dev \
    libqt5svg5-dev \
    libsqlcipher-dev \
    libswresample-dev \
    libswscale-dev \
    libxss-dev \
    qrencode \
    qt5-default \
    qt5-qmake \
    qttools5-dev-tools
```

### toxcore dependencies

Install all of the toxcore dependencies.

<a name="arch-toxcore" />

#### Arch Linux

```bash
sudo pacman -S --needed opus libvpx libsodium
```

<a name="debian-toxcore" />

#### Debian

```bash
sudo apt-get install libtool autotools-dev automake checkinstall check \
libopus-dev libvpx-dev libsodium-dev libavdevice-dev
```

<a name="fedora-toxcore" />

#### Fedora

```bash
sudo dnf install libtool autoconf automake check check-devel libsodium-devel \
opus-devel libvpx-devel
```

<a name="opensuse-toxcore" />

#### openSUSE

```bash
sudo zypper install libsodium-devel libvpx-devel libopus-devel \
patterns-openSUSE-devel_basis
```

<a name="slackware-toxcore" />

#### Slackware

List of all the toxcore dependencies and their SlackBuilds can be found
here: http://slackbuilds.org/repository/14.2/network/toxcore/


<a name="ubuntu-toxcore" />

#### Ubuntu >=15.04

```bash
sudo apt-get install libtool autotools-dev automake checkinstall check \
libopus-dev libvpx-dev libsodium-dev
```


### sqlcipher

If you are not using an old version of Fedora, skip this section, and go
directly to compiling
[**toxcore**](#toxcore-compiling).

```bash
git clone https://github.com/sqlcipher/sqlcipher
cd sqlcipher
./configure --enable-tempstore=yes CFLAGS="-DSQLITE_HAS_CODEC" \
    LDFLAGS="-lcrypto"
make
sudo make install
cd ..
```

### Compile toxcore

Provided that you have all required dependencies installed, you can simply run:

```bash
git clone https://github.com/toktok/c-toxcore.git toxcore
cd toxcore
git checkout v0.1.2
autoreconf -if
./configure
make -j$(nproc)
sudo make install
echo '/usr/local/lib/' | sudo tee -a /etc/ld.so.conf.d/locallib.conf
sudo ldconfig
```

### Compile qTox

**Make sure that all the dependencies are installed.**  If you experience
problems with compiling, it's most likely due to missing dependencies, so please
make sure that you did install *all of them*.

If you are compiling on Fedora 25, you must add libtoxcore to the
`PKG_CONFIG_PATH` environment variable manually:

```
export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig"
```

Run in qTox directory to compile:

```bash
cmake .
make
```

Now you can start compiled qTox with `./qtox`

Congratulations, you've compiled qTox `:)`


#### Debian / Ubuntu / Mint

If the compiling process stops with a missing dependency like:
`... libswscale/swscale.h missing` try:

```bash
apt-file search libswscale/swscale.h
```

And install the package that provides the missing file.
Start make again. Repeat if necessary until all dependencies are installed. If
you can, please note down all additional dependencies you had to install that
aren't listed here, and let us know what is missing `;)`


---

### Building packages

Alternately, qTox now has the experimental and probably-dodgy ability to package
itself (in `.deb` form natively, and `.rpm` form with
[alien](http://joeyh.name/code/alien/)).

After installing the required dependencies, run `bootstrap.sh` and then run the
`buildPackages.sh` script, found in the tools folder. It will automatically get
the packages necessary for building `.deb`s, so be prepared to type your
password for sudo.


<a name="osx" />

## OS X

Supported OS X versions: >=10.8.

Compiling qTox on OS X for development requires 3 tools:
[Xcode](https://developer.apple.com/xcode/),
[Qt 5.4+](https://www.qt.io/qt5-4/) and [homebrew](https://brew.sh).

### Automated Script

You can now set up your OS X system to compile qTox automatically thanks to the
script in: `./osx/qTox-Mac-Deployer-ULTIMATE.sh`

This script can be run independently of the qTox repo and is all that's needed
to build from scratch on OS X.

To use this script you must launch terminal which can be found:
`Applications > Utilities > Terminal.app`

If you wish to lean more you can run `./qTox-Mac-Deployer-ULTIMATE.sh -h`

Note that the script will revert any non-committed changes to qTox repository
during the `update` phase.

#### First Run / Install

If you are running the script for the first time you will want to make sure your
system is ready. To do this simply run `./qTox-Mac-Deployer-ULTIMATE.sh -i` to
run you through the automated install set up.

After running the installation setup you are now ready to build qTox from
source, to do this simply run: `./qTox-Mac-Deployer-ULTIMATE.sh -b`

If there aren't any errors then you'll find a locally working qTox application
in your home folder under `~/qTox-Mac_Build`

#### Updating

If you want to update your application for testing purposes or you want to run a
nightly build setup then run: `./qTox-Mac-Deployer-ULTIMATE.sh -u` and follow
the prompts. (NOTE: If you know you updated the repos before running this hit Y)
followed by `./qTox-Mac-Deployer-ULTIMATE.sh -b` to build the application once
more. (NOTE: This will delete your previous build.)

#### Deploying

OS X requires an extra step to make the `qTox.app` file shareable on a system
that doesn't have the required libraries installed already.

If you want to share the build you've made with your other friends who use OS X
then simply run: `./qTox-Mac-Deployer-ULTIMATE.sh -d`

### Manual Compiling
#### Required Libraries

Install homebrew if you don't have it:

```bash
ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

First, let's install the dependencies available via `brew`.

```bash
brew install git ffmpeg qrencode libtool automake autoconf check qt5 libvpx \
opus sqlcipher libsodium
```

Next, install
[toxcore](https://github.com/toktok/c-toxcore/blob/master/INSTALL.md#osx)

Then, clone qTox:

```bash
git clone https://github.com/qTox/qTox
```

Finally, copy all required files. Whenever you update your brew packages, you
may skip all of the above steps and simply run the following commands:

```bash
cd ./git/qTox
sudo bash bootstrap-osx.sh
```

#### Compiling

You can build qTox with Qt Creator
[seperate download](http://www.qt.io/download-open-source/#section-6) or
manually with cmake

With that; in your terminal you can compile qTox in the git dir:

```bash
cmake .
make
```

Or a cleaner method would be to:

```bash
cd ./git/dir/qTox
mkdir ./build
cd build
cmake ..
```

#### Deploying

If you compiled qTox properly you can now deploy the `qTox.app` that's created
where you built qTox so you can distribute the package.

Using your qt5 homebrew installation from the build directory:

```bash
/usr/local/Cellar/qt5/5.5.1_2/bin/macdeployqt ./qTox.app
```

#### Running qTox

You've got 2 choices, either click on the qTox app that suddenly exists, or do
the following:

```bash
qtox.app/Contents/MacOS/qtox
```

Enjoy the snazzy CLI output as your friends and family congratulate you on
becoming a hacker

<a name="windows" />

## Windows

### Cross-compile from Linux

See [`windows/cross-compile`](windows/cross-compile).

### Native

#### Qt

Download the Qt online installer for Windows from
[qt.io](https://www.qt.io/download-open-source/). While installation you have
to assemble your Qt toolchain. Take the most recent version of Qt compiled with
MinGW. Although the installer provides its own bundled MinGW compiler toolchain
its recommend installing it separately because Qt is missing MSYS which is
needed to compile and install OpenAL. Thus you can - if needed - deselect the
tab `Tools`. The following steps assume that Qt is installed at `C:\Qt`. If you
decided to choose another location, replace corresponding parts.

#### MinGW

Download the MinGW installer for Windows from
[sourceforge.net](http://sourceforge.net/projects/mingw/files/Installer/). Make
sure to install MSYS (a set of Unix tools for Windows). The following steps
assume that MinGW is installed at `C:\MinGW`. If you decided to choose another
location, replace corresponding parts. Select `mingw-developer-toolkit`, 
`mingw32-base`, `mingw32-gcc-g++`, `msys-base` and `mingw32-pthreads-w32` 
packages using MinGW Installation Manager (`mingw-get.exe`). Check that the 
version of MinGW, corresponds to the version of the QT component!

#### Wget

Download the Wget installer for Windows from
http://gnuwin32.sourceforge.net/packages/wget.htm. Install them. The following
steps assume that Wget is installed at `C:\Program Files (x86)\GnuWin32\`. If you
decided to choose another location, replace corresponding parts.

#### UnZip

Download the UnZip installer for Windows from
http://gnuwin32.sourceforge.net/packages/unzip.htm. Install it. The following
steps assume that UnZip is installed at `C:\Program Files (x86)\GnuWin32\`. If you
decided to choose another location, replace corresponding parts.

#### Setting up Path

Add MinGW/MSYS/CMake binaries to the system path to make them globally
accessible. Open `Control Panel` -> `System and Security` -> `System` ->
`Advanced system settings` -> `Environment Variables...` (or run `sysdm.cpl`
select tab `Advanced system settings` -> button `Environment Variables`). In the
second box search for the `PATH` variable and press `Edit...`. The input box
`Variable value:` should already contain some directories. Each directory is
separated with a semicolon. Extend the input box by adding
`;C:\MinGW\bin;C:\MinGW\msys\1.0\bin;C:\Program Files (x86)\CMake 2.8\bin;C:\Program Files (x86)\GnuWin32\bin`.
The very first semicolon must only be added if it is missing. CMake may be added
by installer automatically. Make sure that paths containing alternative `sh`, 
`bash` implementations such as `C:\Program Files\OpenSSH\bin` are at the end of
`PATH` or build may fail.

#### Cloning the Repository

Clone the repository (https://github.com/qTox/qTox.git) with your preferred Git
client. [SmartGit](http://www.syntevo.com/smartgit/) or
[TorteiseGit](https://tortoisegit.org) are both very nice for this task
(you may need to add `git.exe` to your `PATH` system variable). The
following steps assume that you cloned the repository at `C:\qTox`. If you
decided to choose another location, replace corresponding parts.

#### Getting dependencies

Run `bootstrap.bat` in the previously cloned `C:\qTox` repository. The script will
download the other necessary dependencies, compile them and put them into their
appropriate directories.

Note that there have been detections of false positives by some anti virus software
in the past within some of the libraries used. Please refer to the wiki page
[problematic antiviruses](https://github.com/qTox/qTox/wiki/Problematic-antiviruses)
for more information if you run into troubles on that front.

## Compile-time switches

They are passed as an argument to `cmake` command. E.g. with a switch `SWITCH`
that has value `YES` it would be passed to `cmake` in a following manner:

```bash
cmake -DSWITCH=yes
```

Switches:

- `SMILEYS`, values:
  - if not defined or an unsupported value is passed, all emoticon packs are
    included
  - `DISABLED` – don't include any emoticon packs, custom ones are still loaded
  - `MIN` – minimal support for emoticons, only a single emoticon pack is
    included


[Atk]: https://wiki.gnome.org/Accessibility
[Cairo]: https://www.cairographics.org/
[Check]: https://libcheck.github.io/check/
[CMake]: https://cmake.org/
[DBus Menu]: https://launchpad.net/libdbusmenu
[FFmpeg]: https://www.ffmpeg.org/
[GCC]: https://gcc.gnu.org/
[GdkPixbuf]: https://developer.gnome.org/gdk-pixbuf/
[GLib]: https://wiki.gnome.org/Projects/GLib
[GTK+]: https://www.gtk.org/
[libappindicator]: https://launchpad.net/libappindicator
[libX11]: https://www.x.org/wiki/
[libXScrnSaver]: https://www.x.org/wiki/Releases/ModuleVersions/
[MinGW]: http://www.mingw.org/
[OpenAL Soft]: http://kcat.strangesoft.net/openal.html
[Pango]: http://www.pango.org/
[pkg-config]: https://www.freedesktop.org/wiki/Software/pkg-config/
[qrencode]: https://fukuchi.org/works/qrencode/
[Qt]: https://www.qt.io/
[sqlcipher]: https://www.zetetic.net/sqlcipher/
[toxcore]: https://github.com/TokTok/c-toxcore/
[filteraudio]: https://github.com/irungentoo/filter_audio
