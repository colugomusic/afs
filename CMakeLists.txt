cmake_minimum_required(VERSION 3.30)
project(afs)
enable_testing()
find_package(audiorw REQUIRED)
find_package(ez REQUIRED)
find_package(foonathan_memory REQUIRED)
find_package(Immer REQUIRED)
add_library(afs INTERFACE)
add_library(afs::afs ALIAS afs)
target_sources(afs PUBLIC
	FILE_SET HEADERS
	BASE_DIRS
		include/afs
	FILES
		include/afs/afs.hpp
)
target_link_libraries(afs INTERFACE
	audiorw::audiorw
	ez::ez
	foonathan_memory
	immer
)
if (BUILD_TESTING)
	add_subdirectory(test)
endif()
include(CMakePackageConfigHelpers)
install(TARGETS afs EXPORT afs-targets FILE_SET HEADERS DESTINATION include/afs)
install(EXPORT afs-targets FILE afs-targets.cmake NAMESPACE afs:: DESTINATION lib/cmake/afs)
configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/afs-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/afs-config.cmake"
    INSTALL_DESTINATION lib/cmake/afs
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/afs-config.cmake" DESTINATION lib/cmake/afs)
