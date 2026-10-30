set(SECP ${CMAKE_CURRENT_LIST_DIR}/../third_party/secp256k1)
add_library(camwallet_secp STATIC ${SECP}/src/secp256k1.c
    ${SECP}/src/precomputed_ecmult.c ${SECP}/src/precomputed_ecmult_gen.c)
target_include_directories(camwallet_secp PUBLIC ${SECP}/include)
target_compile_definitions(camwallet_secp PUBLIC SECP256K1_STATIC=1)
# Identical portable 32-bit path on host and RP2350; no experimental ARM asm.
target_compile_definitions(camwallet_secp PRIVATE USE_FORCE_WIDEMUL_INT64=1
    ENABLE_MODULE_RECOVERY=1 ECMULT_WINDOW_SIZE=4 COMB_BLOCKS=11 COMB_TEETH=6)
target_compile_options(camwallet_secp PRIVATE -O2 -ffunction-sections -fdata-sections -fstack-usage)

set(TREZOR ${CMAKE_CURRENT_LIST_DIR}/../third_party/trezor_crypto)
add_library(camwallet_hash STATIC ${TREZOR}/sha2.c ${TREZOR}/sha3.c
    ${TREZOR}/hmac.c ${TREZOR}/ripemd160.c ${TREZOR}/memzero.c)
target_include_directories(camwallet_hash PUBLIC ${TREZOR})
target_compile_definitions(camwallet_hash PUBLIC USE_KECCAK=1)
target_compile_options(camwallet_hash PRIVATE -ffunction-sections -fdata-sections)

add_library(camwallet_wallet STATIC wallet.c)
target_include_directories(camwallet_wallet PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(camwallet_wallet PUBLIC camwallet_secp camwallet_hash camwallet_evm)
target_compile_options(camwallet_wallet PRIVATE -Wall -Wextra -Werror -fstack-usage)
