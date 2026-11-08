cmake_minimum_required(VERSION 3.26)

project(Tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

# Party rules under test: compiled from MainServer (they depend only on the Common have_api headers).
file(GLOB_RECURSE PARTY_MODE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/MainServer/src/HaveApi/Modes/*.cpp
)
# Lava geometry, room membership and combat rules of CastServer (they depend only on the Common headers too).
file(GLOB CAST_COMBAT_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/CastServer/src/Combat/*.cpp ${CMAKE_SOURCE_DIR}/CastServer/src/Connection/*.cpp ${CMAKE_SOURCE_DIR}/CastServer/src/Match/*.cpp ${CMAKE_SOURCE_DIR}/CastServer/src/Position/*.cpp)
set(CAST_PARTY_SOURCES ${CMAKE_SOURCE_DIR}/CastServer/src/Rooms/PartyLava.cpp ${CMAKE_SOURCE_DIR}/CastServer/src/Rooms/PartyZone.cpp ${CMAKE_SOURCE_DIR}/CastServer/src/Rooms/PartyStorm.cpp
    ${CMAKE_SOURCE_DIR}/CastServer/src/Rooms/RoomMembership.cpp ${CAST_COMBAT_SOURCES})
set(IPC_TLS_SOURCES ${CMAKE_SOURCE_DIR}/Common/src/Network/IpcTls.cpp)
# Mode logs: daily files per party game (the process-wide writer, ModeLogWriter.cpp, needs the Logger: not tested)
set(MODE_LOG_SOURCES ${CMAKE_SOURCE_DIR}/Common/src/Utils/DailyLogFiles.cpp ${CMAKE_SOURCE_DIR}/Common/src/HaveApi/ModeLog.cpp)
add_executable(Tests ${TEST_SOURCES} ${PARTY_MODE_SOURCES} ${CAST_PARTY_SOURCES} ${IPC_TLS_SOURCES} ${MODE_LOG_SOURCES})

# Visual Studio: show the files in their folders (include/…, src/…) instead of Header Files / Source Files.
source_group(TREE ${CMAKE_CURRENT_LIST_DIR} FILES ${TEST_SOURCES})
source_group(TREE ${CMAKE_SOURCE_DIR}/MainServer/src PREFIX "MainServer" FILES ${PARTY_MODE_SOURCES})
source_group(TREE ${CMAKE_SOURCE_DIR}/CastServer/src PREFIX "CastServer" FILES ${CAST_PARTY_SOURCES})

find_path(JWT_CPP_INCLUDE_DIRS "jwt-cpp/base.h") # Utils/JwtKeys.h
target_include_directories(Tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Common/include
        ${CMAKE_SOURCE_DIR}/Common/include/Utils
        ${CMAKE_SOURCE_DIR}/MainServer/include
        ${CMAKE_SOURCE_DIR}/CastServer/include
        ${JWT_CPP_INCLUDE_DIRS}
)

target_link_libraries(Tests PRIVATE GTest::gtest GTest::gtest_main cryptopp::cryptopp) # cryptopp: HaveApi/DllProof.h
target_link_libraries(Tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(Tests PRIVATE bcrypt unofficial-sodium::sodium) # Utils/PasswordHash.h
if (WIN32)
    target_link_libraries(Tests PRIVATE "bcrypt.lib" ws2_32 mswsock)
endif()

if (MSVC)
    target_compile_options(Tests PRIVATE /W4 /WX /permissive-)
else()
    # -Wno-stringop-overflow: GCC 12..14 false positive at -O3 on MessageWriter::append's vector::insert (the layout
    # tests check the exact sizes)
    target_compile_options(Tests PRIVATE -Wall -Wextra -Werror -Wno-stringop-overflow)
endif()

include(GoogleTest)
gtest_discover_tests(Tests)
