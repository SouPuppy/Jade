add_executable(JadeSmokeTests SmokeTests.cpp)
target_link_libraries(JadeSmokeTests PRIVATE Jade::Jade GTest::gtest_main)
jade_configure_target(JadeSmokeTests)
jade_deploy_runtime(JadeSmokeTests)
