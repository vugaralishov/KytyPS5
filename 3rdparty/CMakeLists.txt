set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(cpuinfo)

include(FetchContent)

# The Bluetooth DualSense transports speaker audio as fixed-size Opus packets.
set(kyty_build_testing_saved "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
if(MSVC AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
	# Opus assumes MSVC accepts SSE4.1 intrinsics without per-file flags, but
	# clang-cl rejects them in its baseline x64 files. Its AVX2 dispatch also
	# depends on the SSE4.1 declarations, so retain the x64 SSE2 baseline only.
	set(OPUS_X86_MAY_HAVE_SSE4_1 OFF CACHE BOOL "" FORCE)
	set(OPUS_X86_MAY_HAVE_AVX2 OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(opus
	GIT_REPOSITORY https://github.com/xiph/opus.git
	GIT_TAG ddbe48383984d56acd9e1ab6a090c54ca6b735a6
	GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(opus)
set(BUILD_TESTING "${kyty_build_testing_saved}")
install(FILES "${opus_SOURCE_DIR}/COPYING" DESTINATION "licenses/opus")

# The guest instruction patcher needs a decoder/encoder and a small
# trampoline assembler. Pin the same revisions used by shadPS4's implementation.
set(XBYAK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	xbyak
	GIT_REPOSITORY https://github.com/herumi/xbyak.git
	GIT_TAG 44a72f369268f7d552650891b296693e91db86bb
	GIT_SHALLOW FALSE
)

set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	zydis
	GIT_REPOSITORY https://github.com/zyantific/zydis.git
	GIT_TAG 120e0e705f8e3b507dc49377ac2879979f0d545c
	GIT_SUBMODULES_RECURSE TRUE
	GIT_SHALLOW FALSE
)

FetchContent_MakeAvailable(xbyak zydis)
target_compile_definitions(xbyak INTERFACE XBYAK_NO_EXCEPTION)

# ZArchive provides random-access reads for compressed game dumps. Build only
# the reader/writer library and its zstd dependency; the command-line tool is
# not part of KytyPS5's runtime distribution.
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	zstd
	GIT_REPOSITORY https://github.com/facebook/zstd.git
	GIT_TAG f8745da6ff1ad1e7bab384bd1f9d742439278e99
	GIT_SHALLOW FALSE
	SOURCE_SUBDIR build/cmake
)
FetchContent_Declare(
	zarchive_source
	GIT_REPOSITORY https://github.com/Exzap/ZArchive.git
	GIT_TAG d2c717730092c7bf8cbb033b12fd4001b7c4d932
	GIT_SHALLOW FALSE
	PATCH_COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/zarchive-reader.patch"
	SOURCE_SUBDIR kyty-library-only
)
FetchContent_MakeAvailable(zstd zarchive_source)

add_library(zarchive STATIC
	"${zarchive_source_SOURCE_DIR}/src/zarchivereader.cpp"
	"${zarchive_source_SOURCE_DIR}/src/zarchivewriter.cpp"
	"${zarchive_source_SOURCE_DIR}/src/sha_256.c"
)
add_library(ZArchive::zarchive ALIAS zarchive)
target_include_directories(zarchive SYSTEM PUBLIC "${zarchive_source_SOURCE_DIR}/include")
target_include_directories(zarchive PRIVATE "${zstd_SOURCE_DIR}/lib")
target_link_libraries(zarchive PRIVATE libzstd_static)
target_compile_features(zarchive PRIVATE cxx_std_20)
foreach(third_party_target zarchive libzstd_static)
	if(NOT TARGET ${third_party_target})
		continue()
	endif()
	if(MSVC)
		target_compile_options(${third_party_target} PRIVATE /W0)
	else()
		target_compile_options(${third_party_target} PRIVATE -w)
	endif()
endforeach()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
if(APPLE)
	# SDL's built-in iconv fallback does not support CP932.
	set(SDL_SYSTEM_ICONV ON CACHE BOOL "Use system iconv for CP932 conversion" FORCE)
endif()
add_subdirectory(SDL3)

add_library(Vulkan-Headers INTERFACE)
add_library(Vulkan::Headers ALIAS Vulkan-Headers)
target_include_directories(Vulkan-Headers INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}/Vulkan-Headers/include"
)

add_library(imgui STATIC
	imgui/imgui.cpp
	imgui/imgui_draw.cpp
	imgui/imgui_tables.cpp
	imgui/imgui_widgets.cpp
	imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends"
)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_link_libraries(imgui PUBLIC Vulkan::Headers)
if(MSVC)
	target_compile_options(imgui PRIVATE /W0)
else()
	target_compile_options(imgui PRIVATE -w)
endif()

set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
set(SPIRV-Headers_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SPIRV-Headers" CACHE PATH "" FORCE)
add_subdirectory(SPIRV-Tools)

add_library(spirv-tools ALIAS SPIRV-Tools-static)
add_library(spirv-tools-opt ALIAS SPIRV-Tools-opt)

add_subdirectory(xxHash/build/cmake)

set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_MODULE OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(fmt EXCLUDE_FROM_ALL)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
add_subdirectory(nlohmann_json EXCLUDE_FROM_ALL)

set(SPDLOG_BUILD_ALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_WARNINGS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
set(SPDLOG_NO_EXCEPTIONS ON CACHE BOOL "" FORCE)
if(WIN32)
	set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(spdlog EXCLUDE_FROM_ALL)

if (NOT TARGET FFmpeg::ffmpeg)
	# On macOS the target arch is driven by CMAKE_OSX_ARCHITECTURES, not the host
	# CMAKE_SYSTEM_PROCESSOR (which reports arm64 even for an x86_64/Rosetta build).
	if(APPLE AND CMAKE_OSX_ARCHITECTURES)
		if(CMAKE_OSX_ARCHITECTURES MATCHES "^(x86_64)$")
			set(ARCHITECTURE x86_64)
		elseif(CMAKE_OSX_ARCHITECTURES MATCHES "^(arm64)$")
			set(ARCHITECTURE arm64)
		else()
			set(ARCHITECTURE x86_64 arm64) # universal
		endif()
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
		set(ARCHITECTURE arm64)
	else()
		set(ARCHITECTURE x86_64)
	endif()

	# ffmpeg-core downloads a prebuilt package without checking its hash. Fetch the same file
	# into the path it expects first, verified against the release digests recorded below, so
	# the submodule finds a trusted archive and skips its own download. The asset name and
	# release tag are computed exactly as in ffmpeg-core/CMakeLists.txt.
	if(NOT FFMPEG_PREBUILT_DIR)
		if(WIN32 AND NOT MINGW)
			set(kyty_ffmpeg_os windows)
		elseif(APPLE)
			set(kyty_ffmpeg_os macos)
		elseif(UNIX)
			set(kyty_ffmpeg_os linux)
		elseif(MINGW)
			set(kyty_ffmpeg_os mingw)
		endif()
		if(ARCHITECTURE STREQUAL "x86_64")
			set(kyty_ffmpeg_arch x64)
		elseif(ARCHITECTURE STREQUAL "arm64")
			set(kyty_ffmpeg_arch arm64)
		else()
			set(kyty_ffmpeg_arch universal)
		endif()
		set(kyty_ffmpeg_asset "ffmpeg-${kyty_ffmpeg_os}-${kyty_ffmpeg_arch}.zip")
		execute_process(
			COMMAND git rev-parse --short=12 HEAD
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-core"
			OUTPUT_VARIABLE kyty_ffmpeg_sha
			OUTPUT_STRIP_TRAILING_WHITESPACE)

		# SHA-256 of the release assets of KytyPS5/ext-ffmpeg-core, tag 9ac4cfd195f1. Update
		# this table together with the submodule.
		set(kyty_ffmpeg_9ac4cfd195f1_ffmpeg-windows-x64.zip
			32839a244a418063f6fb4bbe55585bc66ea17a859b920cae0e3aa1eab9398c09)
		set(kyty_ffmpeg_9ac4cfd195f1_ffmpeg-windows-arm64.zip
			bf6347986ab50135fa379d5596b57fc29cdb7735f59adc2188871c52daee1267)
		set(kyty_ffmpeg_9ac4cfd195f1_ffmpeg-linux-x64.zip
			f4659ad09e6f2b99ecf0227ca56fe4bbb34572cdd8d85ff85384f74ba38f23ef)
		set(kyty_ffmpeg_9ac4cfd195f1_ffmpeg-linux-arm64.zip
			01e39e8a20bbc6002cbe02024a357c39ec2104485bfee8e27a66b1d443e352a0)
		set(kyty_ffmpeg_9ac4cfd195f1_ffmpeg-macos-x64.zip
			b92c60ac99f9b2277f62d42e7424c24bc991d5723f4fa388a5d2e452be18f40b)
		set(kyty_ffmpeg_9ac4cfd195f1_ffmpeg-macos-arm64.zip
			458933af900b2bf0bf3d4422e3ae1c7aef46f671cd4af99c42ffe99d43ca9170)
		set(kyty_ffmpeg_9ac4cfd195f1_ffmpeg-macos-universal.zip
			4f7eccb838b0177cee9edbae671079888469e22044379516b95916c294b406e3)
		set(kyty_ffmpeg_9ac4cfd195f1_ffmpeg-mingw-x64.zip
			e4be9938b321da5f53e248d8ae947c984390e09d0f59fb6855644a972126e26b)

		set(kyty_ffmpeg_hash "${kyty_ffmpeg_${kyty_ffmpeg_sha}_${kyty_ffmpeg_asset}}")
		if(NOT kyty_ffmpeg_hash)
			message(FATAL_ERROR
				"No recorded SHA-256 for FFmpeg ${kyty_ffmpeg_sha}/${kyty_ffmpeg_asset}. Add it to "
				"3rdparty/CMakeLists.txt, or set FFMPEG_PREBUILT_DIR to an unpacked package.")
		endif()

		set(kyty_ffmpeg_zip "${CMAKE_BINARY_DIR}/externals/${kyty_ffmpeg_sha}-${kyty_ffmpeg_asset}")
		set(kyty_ffmpeg_dir "${CMAKE_BINARY_DIR}/externals/ffmpeg-${kyty_ffmpeg_sha}")
		if(EXISTS "${kyty_ffmpeg_zip}")
			file(SHA256 "${kyty_ffmpeg_zip}" kyty_ffmpeg_actual_hash)
			if(NOT kyty_ffmpeg_actual_hash STREQUAL kyty_ffmpeg_hash)
				# Discard the unverified archive and anything unpacked from it.
				message(WARNING "FFmpeg archive ${kyty_ffmpeg_zip} has SHA-256 "
					"${kyty_ffmpeg_actual_hash}, expected ${kyty_ffmpeg_hash}; downloading it again.")
				file(REMOVE "${kyty_ffmpeg_zip}")
				file(REMOVE_RECURSE "${kyty_ffmpeg_dir}")
			endif()
		endif()
		if(NOT EXISTS "${kyty_ffmpeg_zip}")
			message(STATUS "Downloading FFmpeg libraries...")
			file(DOWNLOAD
				"https://github.com/KytyPS5/ext-ffmpeg-core/releases/download/${kyty_ffmpeg_sha}/${kyty_ffmpeg_asset}"
				"${kyty_ffmpeg_zip}"
				EXPECTED_HASH SHA256=${kyty_ffmpeg_hash}
				SHOW_PROGRESS STATUS kyty_ffmpeg_status)
			list(GET kyty_ffmpeg_status 0 kyty_ffmpeg_status_code)
			if(NOT kyty_ffmpeg_status_code EQUAL 0)
				file(REMOVE "${kyty_ffmpeg_zip}")
				message(FATAL_ERROR "Could not download FFmpeg ${kyty_ffmpeg_sha}: ${kyty_ffmpeg_status}")
			endif()
			file(SHA256 "${kyty_ffmpeg_zip}" kyty_ffmpeg_actual_hash)
			if(NOT kyty_ffmpeg_actual_hash STREQUAL kyty_ffmpeg_hash)
				file(REMOVE "${kyty_ffmpeg_zip}")
				message(FATAL_ERROR "FFmpeg archive ${kyty_ffmpeg_asset} has SHA-256 "
					"${kyty_ffmpeg_actual_hash}, expected ${kyty_ffmpeg_hash}")
			endif()
			# Unpack again from the verified archive.
			file(REMOVE_RECURSE "${kyty_ffmpeg_dir}")
		endif()
	endif()

	add_subdirectory(ffmpeg-core)
	add_library(FFmpeg::ffmpeg ALIAS ffmpeg)
endif()

file(GLOB LIBATRAC9_SOURCES LibAtrac9/C/src/*.c)
add_library(LibAtrac9 STATIC ${LIBATRAC9_SOURCES})
target_include_directories(LibAtrac9 PUBLIC LibAtrac9/C/src)
if(MSVC)
	target_compile_options(LibAtrac9 PRIVATE /W0)
endif()

set(TRACY_STATIC ON CACHE BOOL "" FORCE)
set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
set(TRACY_NO_CRASH_HANDLER ON CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
set(TRACY_DELAYED_INIT ON CACHE BOOL "" FORCE)
set(TRACY_MANUAL_LIFETIME ON CACHE BOOL "" FORCE)
add_subdirectory(tracy EXCLUDE_FROM_ALL)
if (TARGET TracyClient)
	if (MSVC)
		target_compile_options(TracyClient PRIVATE /WX-)
	endif()
	if (CLANG)
		target_compile_options(TracyClient PRIVATE
			-Wno-error
			-Wno-braced-scalar-init
			-Wno-format
			-Wno-unused-variable
			-Wno-unused-function
			-Wno-unused-private-field
			-Wno-unused-value
		)
	endif()
endif()

if (CLANG AND MSVC)
# The prebuilt winpthread binaries are committed to the repository and shipped with every Windows
# build. Refuse to use them if they differ from the recorded files (see
# docs/third-party/winpthread.md for their provenance).
foreach(kyty_winpthread_file
		"bin/libwinpthread-1.dll=78ebbc1480d62098aa66307db2ff139abc07c137bc188f5b73bf7b131492932c"
		"lib/libwinpthread.dll.a=c0302c72dbd07e551c29e968d385cf315bf42ec8334a60b3acf39346e8f972ef")
	string(REPLACE "=" ";" kyty_winpthread_entry "${kyty_winpthread_file}")
	list(GET kyty_winpthread_entry 0 kyty_winpthread_path)
	list(GET kyty_winpthread_entry 1 kyty_winpthread_expected)
	file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/winpthread/${kyty_winpthread_path}" kyty_winpthread_actual)
	if(NOT kyty_winpthread_actual STREQUAL kyty_winpthread_expected)
		message(FATAL_ERROR "3rdparty/winpthread/${kyty_winpthread_path} has SHA-256 "
			"${kyty_winpthread_actual}, expected ${kyty_winpthread_expected}. Update "
			"docs/third-party/winpthread.md and this check only for a reviewed replacement.")
	endif()
endforeach()
add_subdirectory(winpthread)
endif()
