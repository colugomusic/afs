cmake_minimum_required(VERSION 3.30)
project(afs-test)
add_executable(afs-test)
target_sources(afs-test PRIVATE
	src/doctest.h
	src/main.cpp
)
target_link_libraries(afs-test afs::afs)
target_compile_definitions(afs-test PRIVATE
	ASSETS_DIR="${CMAKE_CURRENT_LIST_DIR}/assets"
)
add_test(NAME afs-test COMMAND afs-test)
