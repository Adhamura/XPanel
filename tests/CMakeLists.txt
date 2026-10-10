find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_profilerules tst_profilerules.cpp ../src/ProfileRules.h ../src/ProfileRules.cpp)
target_include_directories(tst_profilerules PRIVATE ../src)
target_link_libraries(tst_profilerules PRIVATE Qt6::Core Qt6::Test)
add_test(NAME profilerules COMMAND tst_profilerules)

qt_add_executable(tst_gyro tst_gyro.cpp ../daemon/GyroMath.h ../daemon/GyroMath.cpp ../daemon/Imu.h ../daemon/Imu.cpp)
target_include_directories(tst_gyro PRIVATE ../daemon)
target_link_libraries(tst_gyro PRIVATE Qt6::Core Qt6::Test)
add_test(NAME gyro COMMAND tst_gyro)
