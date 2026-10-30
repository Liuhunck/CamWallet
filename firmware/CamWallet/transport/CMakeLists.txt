set(CUR_SRC ${CMAKE_CURRENT_LIST_DIR}/../third_party/cur/src)
add_library(camwallet_cur STATIC
    ${CUR_SRC}/ur.c ${CUR_SRC}/ur_encoder.c ${CUR_SRC}/ur_decoder.c
    ${CUR_SRC}/fountain_encoder.c ${CUR_SRC}/fountain_decoder.c
    ${CUR_SRC}/fountain_utils.c ${CUR_SRC}/bytewords.c
    ${CUR_SRC}/crc32.c ${CUR_SRC}/utils.c ${CUR_SRC}/sha256/sha256.c)
target_include_directories(camwallet_cur PUBLIC ${CUR_SRC})
target_compile_definitions(camwallet_cur PRIVATE CAMWALLET_CUR_ALLOCATOR
    UR_MAX_SEQ_LEN=32u UR_MAX_MESSAGE_LEN=768u)
target_compile_options(camwallet_cur PRIVATE
    -include ${CMAKE_CURRENT_LIST_DIR}/cur_memory.h -Werror=unused-result)
add_library(camwallet_transport STATIC ur_transport.c cur_memory.c)
target_include_directories(camwallet_transport PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(camwallet_transport PUBLIC camwallet_cur m)
