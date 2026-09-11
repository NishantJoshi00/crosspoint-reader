add_executable(ArcPlayerTest
  ArcPlayerTest.cpp
  ${REPO_ROOT}/lib/ArcPlayer/ArcGraphics.cpp
  ${REPO_ROOT}/lib/ArcPlayer/ArcPack.cpp
)
target_include_directories(ArcPlayerTest PRIVATE ${REPO_ROOT}/lib/ArcPlayer)
target_link_libraries(ArcPlayerTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ArcPlayerTest)
