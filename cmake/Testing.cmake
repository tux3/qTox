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

auto_test(core toxpk)
auto_test(core toxid)
auto_test(core toxstring)
auto_test(chatlog textformatter)
auto_test(net toxmedata)
auto_test(net bsu)
auto_test(persistence paths)

if (UNIX)
  auto_test(platform posixsignalnotifier)
endif()
