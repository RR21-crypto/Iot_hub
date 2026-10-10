cmake_minimum_required(VERSION 3.16)

# Firmware version: shown in Settings and on the OTA page. Bump it for every release.
set(PROJECT_VER "1.6.1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(anna_display)
