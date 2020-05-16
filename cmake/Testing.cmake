#   Copyright © 2019 by The qTox Project Contributors
#
#   This file is part of qTox, a Qt-based graphical interface for Tox.
#   qTox is libre software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   qTox is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with qTox.  If not, see <http://www.gnu.org/licenses/>

################################################################################
#
# :: Testing
#
################################################################################

enable_testing()

function(auto_test subsystem module)
  add_executable(test_${module}
    test/${subsystem}/${module}_test.cpp)
  target_link_libraries(test_${module}
    ${PROJECT_NAME}_static
    ${CHECK_LIBRARIES}
    Qt5::Test)
  add_test(
    NAME test_${module}
    COMMAND ${TEST_CROSSCOMPILING_EMULATOR} test_${module})
endfunction()

auto_test(core core)
auto_test(core contactid)
auto_test(core toxid)
auto_test(core toxstring)
auto_test(chatlog textformatter)
auto_test(net bsu)
auto_test(persistence paths)
auto_test(persistence dbschema)
auto_test(persistence offlinemsgengine)
auto_test(model friendmessagedispatcher)
auto_test(model groupmessagedispatcher)
auto_test(model messageprocessor)
auto_test(model sessionchatlog)
auto_test(model exiftransform)

if (UNIX)
  auto_test(platform posixsignalnotifier)
endif()
