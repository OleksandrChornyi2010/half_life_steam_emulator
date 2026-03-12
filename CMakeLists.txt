# Based on: https://github.com/ttroy50/cmake-examples/blob/master/03-code-generation/protobuf/CMakeLists.txt
cmake_minimum_required(VERSION 3.6)

# Set the project name
project(half_life_steam_emulator)
add_definitions(-DPROTOBUF_CONSTEXPR=constexpr)
if(MSVC)
  # Set static environment (results in static compile flags) if Visual Studio is used (dynamic by default)
	# Officially recommended solution: https://gitlab.kitware.com/cmake/community/wikis/FAQ#how-can-i-build-my-msvc-application-with-a-static-runtime
	# Should be replaced by a better solution in the future: https://gitlab.kitware.com/cmake/cmake/merge_requests/3211
	foreach(flag_var
		CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
		CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
		if(${flag_var} MATCHES "/MD")
		  string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
		endif(${flag_var} MATCHES "/MD")
	endforeach(flag_var)

	# Disable MSVC++ warning C4996: 'may be unsafe/disable deprecation'
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

# Add option to enable experimental build
option(EMU_EXPERIMENTAL_BUILD "Enable experimental build" OFF)

# Set CXX standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find the protobuf compiler and libraries
include(FindProtobuf)
find_package(Protobuf 3.1.0 REQUIRED)

if(UNIX AND NOT APPLE)
	file(GLOB ABSL_LIBS "/usr/lib32/libabsl_*.a")
	file(GLOB UTF_LIBS "/usr/lib32/libutf8_*.a")

	set(MY_STATIC_PROTO_DEPS
			-Wl,--start-group
			${Protobuf_LIBRARY}
			${ABSL_LIBS}
			${UTF_LIBS}
			-lz
			-Wl,--end-group
	)

	set(Protobuf_LIBRARIES ${MY_STATIC_PROTO_DEPS})
	if(TARGET protobuf::libprotobuf)
		set_target_properties(protobuf::libprotobuf PROPERTIES
				INTERFACE_LINK_LIBRARIES "${MY_STATIC_PROTO_DEPS}")
	endif()

	message(STATUS "Static Protobuf Group enabled with ${Protobuf_LIBRARY}")
endif()
# ----------------------------------------------
# Generate the .h and .cxx files for dll/net.proto

set(PROTO_INPUT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/dll/net.proto")
set(PROTO_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(DESTINATION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dll")

message(STATUS "Force generating Protobuf files in ${PROTO_OUTPUT_DIR}...")
execute_process(
		COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
		--cpp_out=${PROTO_OUTPUT_DIR}
		-I=${CMAKE_CURRENT_SOURCE_DIR}/dll
		${PROTO_INPUT_FILE}
		RESULT_VARIABLE PROTOC_RESULT
)

if(NOT PROTOC_RESULT EQUAL 0)
	message(FATAL_ERROR "Failed to generate net.pb.cc/h using protoc!")
endif()

message(STATUS "Copying generated files to ${DESTINATION_DIR}...")
execute_process(
		COMMAND ${CMAKE_COMMAND} -E copy
		"${PROTO_OUTPUT_DIR}/net.pb.cc"
		"${PROTO_OUTPUT_DIR}/net.pb.h"
		"${DESTINATION_DIR}"
)

set(PROTO_SRCS "${PROTO_OUTPUT_DIR}/net.pb.cc")
set(PROTO_HDRS "${PROTO_OUTPUT_DIR}/net.pb.h")

# We are now generating net.pb.cc/h files within reloading CMake project
# protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS dll/net.proto)

# Print path to generated files
message(STATUS "PROTO_SRCS = ${PROTO_SRCS}")
message(STATUS "PROTO_HDRS = ${PROTO_HDRS}")
message(STATUS "PROTOBUF_INCLUDE_DIRS = ${PROTOBUF_INCLUDE_DIRS}")
message(STATUS "PROTOBUF_LIBRARIES = ${PROTOBUF_LIBRARIES}")
message(STATUS "PROTOBUF_PROTOC_EXECUTABLE = ${PROTOBUF_PROTOC_EXECUTABLE}")

# Setup the lib/exe names for the targets
if(WIN32)
  if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    set(LIB_STEAM_API steam_api64)
	  set(LIB_STEAMCLIENT steamclient64)
	  set(LIB_STEAMNETWORKINGSOCKETS steamnetworkingsockets64)
	  set(BIN_LOBBY_CONNECT lobby_connect64)
	  set(BIN_GENERATE_INTERFACES_FILE generate_interfaces_file64)
  else()
    set(LIB_STEAM_API steam_api)
	  set(LIB_STEAMCLIENT steamclient)
	  set(LIB_STEAMNETWORKINGSOCKETS steamnetworkingsockets)
	  set(BIN_LOBBY_CONNECT lobby_connect)
	  set(BIN_GENERATE_INTERFACES_FILE generate_interfaces_file)
  endif()
elseif(UNIX AND NOT APPLE)
  set(LIB_STEAM_API steam_api)
	set(LIB_STEAMCLIENT steamclient)
	set(LIB_STEAMNETWORKINGSOCKETS steamnetworkingsockets)
	set(BIN_LOBBY_CONNECT lobby_connect)
	set(BIN_GENERATE_INTERFACES_FILE generate_interfaces_file)
else()
	message(FATAL_ERROR "Other platforms not supported...")
endif()

# Gather the files that are shared between multiple targets
file(GLOB DLL_SRC_SHARED
	dll/*.h
	dll/*.cpp
)

file(GLOB DETOURS_SRC_SHARED
	detours/*.cpp
)

if(WIN32)
  file(GLOB OVERLAY_EXPERIMENTAL_SRC_SHARED
    overlay_experimental/*.cpp
    overlay_experimental/windows/*.cpp
    ImGui/*.cpp
    ImGui/impls/*.cpp
    ImGui/impls/windows/*.cpp
    glew/glew.c
  )
elseif(UNIX)
  file(GLOB OVERLAY_EXPERIMENTAL_SRC_SHARED
    overlay_experimental/*.cpp
    overlay_experimental/linux/*.cpp
    ImGui/*.cpp
    ImGui/impls/*.cpp
    ImGui/impls/linux/*.cpp
    glew/glew.c
  )
endif()

###################################################
# Setup for the steam_api(64).dll / libsteam_api.so
###################################################

# Setup the target
add_library(${LIB_STEAM_API}
  SHARED
	$<$<BOOL:${EMU_EXPERIMENTAL_BUILD}>:${DETOURS_SRC_SHARED}>
	$<$<AND:$<BOOL:${EMU_EXPERIMENTAL_BUILD}>,$<BOOL:${EMU_OVERLAY}>>:${OVERLAY_EXPERIMENTAL_SRC_SHARED}>
  ${DLL_SRC_SHARED}
  ${PROTO_SRCS}
  ${PROTO_HDRS}
		dll/vdf_parser.cpp
		dll/vdf_parser.h
		dll/steam_matchmaking.cpp
)

# Check if post build script exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/post_build.sh")
    add_custom_command(TARGET ${LIB_STEAM_API}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Running post-build script for ${LIB_STEAM_API}..."

        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/post_build.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Executing custom steps after build is finished"
        VERBATIM
    )
else()
    message(STATUS "Post-build script NOT found, skipping step for ${LIB_STEAM_API}")
endif()
# Include the required directories
target_include_directories(${LIB_STEAM_API}
  PRIVATE
  ${PROTOBUF_INCLUDE_DIRS}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/ImGui
  ${CMAKE_CURRENT_SOURCE_DIR}/glew/include
)

# Link the required libraries
target_link_libraries(${LIB_STEAM_API}
	PRIVATE
	protobuf::libprotobuf
	$<$<BOOL:${WIN32}>:ws2_32>
	$<$<BOOL:${WIN32}>:iphlpapi>
	$<$<AND:$<BOOL:${WIN32}>,$<BOOL:${EMU_EXPERIMENTAL_BUILD}>,$<BOOL:${EMU_OVERLAY}>>:opengl32.lib>
	$<$<AND:$<BOOL:${WIN32}>,$<BOOL:${EMU_EXPERIMENTAL_BUILD}>,$<BOOL:${EMU_OVERLAY}>>:Winmm.lib>
)

# Add target compile definitions
target_compile_definitions(${LIB_STEAM_API}
	PRIVATE
	$<$<CONFIG:>:EMU_RELEASE_BUILD>
  $<$<CONFIG:Release>:EMU_RELEASE_BUILD>
	$<$<CONFIG:RelWithDebInfo>:EMU_RELEASE_BUILD>
	$<$<CONFIG:MinSizeRel>:EMU_RELEASE_BUILD>
	$<$<BOOL:${EMU_EXPERIMENTAL_BUILD}>:EMU_EXPERIMENTAL_BUILD>
	$<$<BOOL:${EMU_OVERLAY}>:EMU_OVERLAY>
	$<$<AND:$<BOOL:${WIN32}>,$<BOOL:${EMU_EXPERIMENTAL_BUILD}>,$<BOOL:${EMU_OVERLAY}>>:GLEW_STATIC>
)

# Install the target
if(WIN32)
  install(TARGETS
    ${LIB_STEAM_API}
    RUNTIME DESTINATION ./
  )
else()
	install(TARGETS
    ${LIB_STEAM_API}
    LIBRARY DESTINATION ./
  )
endif()

########################################################
# Setup for the steamclient(64).dll / libsteamclient.so?
########################################################

# Setup the target
add_library(${LIB_STEAMCLIENT}
	SHARED
	steamclient.cpp
)

# Add target compile definitions
target_compile_definitions(${LIB_STEAMCLIENT}
	PRIVATE
	$<$<CONFIG:>:EMU_RELEASE_BUILD>
  $<$<CONFIG:Release>:EMU_RELEASE_BUILD>
	$<$<CONFIG:RelWithDebInfo>:EMU_RELEASE_BUILD>
	$<$<CONFIG:MinSizeRel>:EMU_RELEASE_BUILD>
	$<$<BOOL:${EMU_EXPERIMENTAL_BUILD}>:EMU_EXPERIMENTAL_BUILD>
)

# Install the target
if(WIN32)
  install(TARGETS
    ${LIB_STEAMCLIENT}
    RUNTIME DESTINATION ./
  )
else()
	install(TARGETS
    ${LIB_STEAMCLIENT}
    LIBRARY DESTINATION ./
  )
endif()

if(NOT WIN32)
  message(STATUS "Target library 'steamclient' is only suported for windows at this time... Disabling Build ALL inclusion for this target")
	set_target_properties(${LIB_STEAMCLIENT} PROPERTIES EXCLUDE_FROM_ALL 1 EXCLUDE_FROM_DEFAULT_BUILD 1)
endif()

##############################################################################
# Setup for the steamnetworkingsockets(64).dll / libsteamnetworkingsockets.so?
##############################################################################

# Setup the target
add_library(${LIB_STEAMNETWORKINGSOCKETS}
	SHARED
	steamnetworkingsockets.cpp
)

# Add target compile definitions
target_compile_definitions(${LIB_STEAMNETWORKINGSOCKETS}
	PRIVATE
	$<$<CONFIG:>:EMU_RELEASE_BUILD>
  $<$<CONFIG:Release>:EMU_RELEASE_BUILD>
	$<$<CONFIG:RelWithDebInfo>:EMU_RELEASE_BUILD>
	$<$<CONFIG:MinSizeRel>:EMU_RELEASE_BUILD>
	$<$<BOOL:${EMU_EXPERIMENTAL_BUILD}>:EMU_EXPERIMENTAL_BUILD>
)

# Setup install rules for the target
if(WIN32)
  install(TARGETS
    ${LIB_STEAMNETWORKINGSOCKETS}
    RUNTIME DESTINATION ./
  )
else()
	install(TARGETS
    ${LIB_STEAMNETWORKINGSOCKETS}
    LIBRARY DESTINATION ./
  )
endif()

if(NOT WIN32)
  message(STATUS "Target library 'steamnetworkingsockets' is only supported for windows at this time... Disabling Build ALL inclusion for this target")
	set_target_properties(${LIB_STEAMNETWORKINGSOCKETS} PROPERTIES EXCLUDE_FROM_ALL 1 EXCLUDE_FROM_DEFAULT_BUILD 1)
endif()

###########################################################################
# Setup for the lobby_connect(64).exe / lobby_connect
###########################################################################

# Setup the target
add_executable(${BIN_LOBBY_CONNECT}
	lobby_connect.cpp
	$<$<BOOL:${EMU_EXPERIMENTAL_BUILD}>:${DETOURS_SRC_SHARED}>
	${DLL_SRC_SHARED}
	${PROTO_SRCS}
	${PROTO_HDRS}
)

target_include_directories(${BIN_LOBBY_CONNECT}
	PRIVATE
	${PROTOBUF_INCLUDE_DIRS}
	${CMAKE_CURRENT_BINARY_DIR}
)

# Link the required libraries
target_link_libraries(${BIN_LOBBY_CONNECT}
	PRIVATE
	protobuf::libprotobuf
	$<$<BOOL:${WIN32}>:ws2_32>
	$<$<BOOL:${WIN32}>:iphlpapi>
	$<$<BOOL:${WIN32}>:comdlg32>
  ${CMAKE_DL_LIBS}
	-debug:none
)

# Add target compile definitions
target_compile_definitions(${BIN_LOBBY_CONNECT}
	PRIVATE
	NO_DISK_WRITES
	LOBBY_CONNECT
	$<$<CONFIG:>:EMU_RELEASE_BUILD>
  $<$<CONFIG:Release>:EMU_RELEASE_BUILD>
	$<$<CONFIG:RelWithDebInfo>:EMU_RELEASE_BUILD>
	$<$<CONFIG:MinSizeRel>:EMU_RELEASE_BUILD>
	$<$<BOOL:${EMU_EXPERIMENTAL_BUILD}>:EMU_EXPERIMENTAL_BUILD>
)

# Setup install rules for the target
install(TARGETS
  ${BIN_LOBBY_CONNECT}
  RUNTIME DESTINATION lobby_connect/
)

###########################################################################
# Setup for the generate_interfaces_file(64).exe / generate_interfaces_file
###########################################################################

# Setup the target
add_executable(${BIN_GENERATE_INTERFACES_FILE}
	generate_interfaces_file.cpp
)

# Link the required libraries
target_link_libraries(${BIN_GENERATE_INTERFACES_FILE}
	PRIVATE
	-debug:none
)

# Setup install rules for the target
install(TARGETS
  ${BIN_GENERATE_INTERFACES_FILE}
  RUNTIME DESTINATION tools/
)

###########################################################################
# Installation setup for non target files and directories
###########################################################################

install(FILES 
  Readme_lobby_connect.txt
  DESTINATION lobby_connect/
)

install(FILES 
  scripts/find_interfaces.sh
  scripts/find_interfaces.ps1
  Readme_generate_interfaces.txt
  DESTINATION tools/
)

install(FILES 
  Readme_release.txt
  files_example/steam_appid.EDIT_AND_RENAME.txt
  files_example/steam_interfaces.EXAMPLE.txt
  $<$<BOOL:${EMU_EXPERIMENTAL_BUILD}>:${PROJECT_SOURCE_DIR}/Readme_experimental.txt>
  $<$<CONFIG:Debug>:${PROJECT_SOURCE_DIR}/Readme_debug.txt>
  DESTINATION ./
)

install(DIRECTORY 
  files_example/steam_settings.EXAMPLE
  DESTINATION ./
)
