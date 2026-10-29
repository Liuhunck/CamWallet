cmake_minimum_required(VERSION 3.13)
project(camwallet_qr_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()
add_subdirectory(../CamWallet/qr qr-library)
add_executable(qr_roundtrip_test qr_roundtrip_test.c)
target_link_libraries(qr_roundtrip_test PRIVATE camwallet_qr)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(qr_roundtrip_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME qr_roundtrip COMMAND qr_roundtrip_test)

add_executable(scan_preview_test
    scan_preview_test.c
    ../CamWallet/ui/scan_preview.c
    ../CamWallet/third_party/waveshare/font16.c)
target_include_directories(scan_preview_test PRIVATE
    ../CamWallet ../CamWallet/third_party/waveshare)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(scan_preview_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME scan_preview COMMAND scan_preview_test)
