cmake_minimum_required(VERSION 3.13)
project(camwallet_qr_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()
add_subdirectory(../CamWallet/qr qr-library)
add_subdirectory(../CamWallet/transport transport-library)
add_subdirectory(../CamWallet/evm evm-library)
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

add_executable(evm_review_test evm_review_test.c
    ../CamWallet/ui/evm_review.c ../CamWallet/ui/evm_review_render.c
    ../CamWallet/drivers/touch_report.c ../CamWallet/third_party/waveshare/font16.c)
target_include_directories(evm_review_test PRIVATE ../CamWallet ../CamWallet/third_party/waveshare)
target_link_libraries(evm_review_test PRIVATE camwallet_evm)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(evm_review_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME evm_review COMMAND evm_review_test)

add_executable(ur_transport_test ur_transport_test.c)
target_link_libraries(ur_transport_test PRIVATE camwallet_transport camwallet_evm camwallet_qr)
add_test(NAME ur_transport COMMAND ur_transport_test)
add_executable(ur_bridge ur_bridge.c ../CamWallet/ui/evm_review.c)
target_include_directories(ur_bridge PRIVATE ../CamWallet)
target_link_libraries(ur_bridge PRIVATE camwallet_transport camwallet_evm)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ur_transport_test PRIVATE -Wall -Wextra -Werror)
    target_compile_options(ur_bridge PRIVATE -Wall -Wextra -Werror)
    target_compile_options(camwallet_transport PRIVATE -Wall -Wextra -Werror)
endif()
