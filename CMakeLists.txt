cmake_minimum_required(VERSION 3.10)
if (NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/examples)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/examples)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/examples)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_SOURCE_DIR}/examples)
endif()

project(aspromise)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo")
		
file(GLOB_RECURSE SOURCE
    ${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/include/*.*
    ${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/source/*.h
    ${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/source/*.cpp)
list(APPEND SOURCE
    "${PROJECT_SOURCE_DIR}/examples/promises.as"
    "${PROJECT_SOURCE_DIR}/examples/promises.cpp"
    "${PROJECT_SOURCE_DIR}/src/aspromise.hpp")
if (MSVC)
	if (CMAKE_SIZEOF_VOID_P EQUAL 8)
		if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^arm64")
			list(APPEND SOURCE "${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/source/as_callfunc_arm64_msvc.asm")
		else()
			list(APPEND SOURCE "${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/source/as_callfunc_x64_msvc_asm.asm")
		endif()
	elseif (${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
		list(APPEND SOURCE "${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/source/as_callfunc_arm_msvc.asm")
	endif()
elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^arm")
	list(APPEND SOURCE "${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/source/as_callfunc_arm_gcc.S")
	list(APPEND SOURCE "${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/source/as_callfunc_arm_vita.S")
	list(APPEND SOURCE "${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/source/as_callfunc_arm_xcode.S")
elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^aarch64")
    if (APPLE)
	    list(APPEND SOURCE "${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/source/as_callfunc_arm64_xcode.S")
    else()
        list(APPEND SOURCE "${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/source/as_callfunc_arm64_gcc.S")
    endif()
endif()
foreach(ITEM IN ITEMS ${SOURCE})
    get_filename_component(ITEM_PATH "${ITEM}" PATH)
    string(REPLACE "${PROJECT_SOURCE_DIR}" "" ITEM_GROUP "${ITEM_PATH}")
    string(REPLACE "/" "\\" ITEM_GROUP "${ITEM_GROUP}")
    source_group("${ITEM_GROUP}" FILES "${ITEM}")
endforeach()

add_executable(aspromise ${SOURCE})
set_target_properties(aspromise PROPERTIES
    OUTPUT_NAME "aspromise"
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION})
if (NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    if (NOT (WIN32 AND MINGW))
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wformat=0 -std=c++14 -fexceptions")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-implicit-function-declaration -fexceptions")
		if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
			if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
				set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")
				set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0")
			else()
				set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")
				set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2")
			endif()
		endif()
	else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mwindows -D_WIN32_WINNT=0x0600")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mwindows -D_WIN32_WINNT=0x0600")
    endif()
else()
    set(CMAKE_CXX_FLAGS_DEBUG "/MDd /MP /Zi /Ob0 /Od")
    set(CMAKE_CXX_FLAGS_RELEASE "/MD /MP /O2 /Ob2 /DNDEBUG")
    set(CMAKE_C_FLAGS_DEBUG "/MDd /MP /Zi /Ob0 /Od")
    set(CMAKE_C_FLAGS_RELEASE "/MD /MP /O2 /Ob2 /DNDEBUG")
    target_compile_definitions(aspromise PRIVATE
            -D_CRT_SECURE_NO_WARNINGS
            -D_SCL_SECURE_NO_WARNINGS)
endif()
if (NOT MSVC)
	enable_language(ASM)
	set(CMAKE_ASM_FLAGS "${CFLAGS} -x assembler-with-cpp")
elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
	enable_language(ASM_MASM)
endif()
target_include_directories(aspromise PRIVATE ${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/include)
target_include_directories(aspromise PRIVATE ${PROJECT_SOURCE_DIR}/examples/angelscript/sdk/angelscript/source)
target_compile_definitions(aspromise PRIVATE
        -DANGELSCRIPT_EXPORT
        -DAS_USE_STLNAMES)