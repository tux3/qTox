#!/bin/bash
#
#    Copyright © 2016-2017 The qTox Project Contributors
#
#    This program is free software: you can redistribute it and/or modify
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


# script to change versions in the files for osx and windows "packages"
# 
# it should be run before releasing a new version
#
# NOTE: it checkouts the files before appending a version to them!
#
# requires:
#  * GNU sed

# usage:
#
#   ./$script $version
#
# $version has to be composed of at least one number/dot


set -eu -o pipefail


update_windows() {
    ( cd windows
        ./qtox-nsi-version.sh "$@" )
}

update_osx() {
    ( cd osx
        ./update-plist-version.sh "$@" )
}

# exit if supplied arg is not a version
is_version() {
    if [[ ! $@ =~ [0-9\\.]+ ]]
    then
        echo "Not a version: $@"
        exit 1
    fi
}

main() {
    is_version "$@"

    # osx cannot into proper sed
    if [[ ! "$OSTYPE" == "darwin"* ]]
    then
        update_osx "$@"
        update_windows "$@"
    else
        # TODO: actually check whether there is a GNU sed on osx
        echo "OSX's sed not supported. Get a proper one."
    fi
}
main "$@"
