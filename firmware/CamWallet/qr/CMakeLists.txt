add_library(camwallet_qr STATIC
    qr.c
    ../third_party/quirc/quirc.c
    ../third_party/quirc/identify.c
    ../third_party/quirc/decode.c
    ../third_party/quirc/version_db.c
    ../third_party/qrcodegen/qrcodegen.c
)

target_include_directories(camwallet_qr
    PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../third_party/qrcodegen
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../third_party/quirc
)

# RP2350 Arm cores have a single-precision FPU. Preserve upstream defaults for
# region counts so the grayscale image and region pixels share one allocation.
target_compile_definitions(camwallet_qr PRIVATE QUIRC_FLOAT_TYPE=float QUIRC_USE_TGMATH)
target_link_libraries(camwallet_qr PUBLIC m)
