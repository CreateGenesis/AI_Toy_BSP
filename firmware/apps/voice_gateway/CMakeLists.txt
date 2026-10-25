if (CONFIG_ENABLE_AUDIO_CODECS STREQUAL "y")
set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(APP_NAME ${APP_PATH} NAME)
aux_source_directory(${APP_PATH}/src APP_SRCS)
list(APPEND APP_SRCS
    ${TOP_SOURCE_DIR}/../firmware/third_party/auth_device/src/auth_device.c
    ${TOP_SOURCE_DIR}/../firmware/third_party/auth_device/src/auth_sdk_port.c
)

add_library(${EXAMPLE_LIB})
target_sources(${EXAMPLE_LIB} PRIVATE ${APP_SRCS})
# minimp3 ships with TuyaOpen's audio player; only its header is used here.
target_include_directories(${EXAMPLE_LIB} PRIVATE ${TOP_SOURCE_DIR}/src/audio_player/src/decoder/minimp3)
target_include_directories(${EXAMPLE_LIB} PRIVATE ${TOP_SOURCE_DIR}/../firmware/third_party/auth_device/include)
else()
message(FATAL_ERROR "voice_gateway requires ENABLE_AUDIO_CODECS")
endif()
