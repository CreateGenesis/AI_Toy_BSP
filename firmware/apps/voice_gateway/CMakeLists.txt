if (CONFIG_ENABLE_AUDIO_CODECS STREQUAL "y")
set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(APP_NAME ${APP_PATH} NAME)
aux_source_directory(${APP_PATH}/src APP_SRCS)

add_library(${EXAMPLE_LIB})
target_sources(${EXAMPLE_LIB} PRIVATE ${APP_SRCS})
# minimp3 ships with TuyaOpen's audio player; only its header is used here.
target_include_directories(${EXAMPLE_LIB} PRIVATE ${TOP_SOURCE_DIR}/src/audio_player/src/decoder/minimp3)
else()
message(FATAL_ERROR "voice_gateway requires ENABLE_AUDIO_CODECS")
endif()
