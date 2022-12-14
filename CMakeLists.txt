cmake_minimum_required(VERSION 3.19)
project(mrd_stream)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR})

add_compile_options(-Wall -Wextra -pedantic -Werror)

find_package(ISMRMRD CONFIG REQUIRED)
find_package(FFTW3 COMPONENTS single)
find_package(fmt CONFIG REQUIRED)

add_executable(mrd_stream_recon cpp/mrd_stream_recon.cpp)
target_include_directories(mrd_stream_recon PRIVATE ${FFTW3_INCLUDE_DIR})
target_link_libraries(mrd_stream_recon ${FFTW3_LIBRARIES} ISMRMRD::ISMRMRD fmt)
install(TARGETS mrd_stream_recon DESTINATION bin)

add_executable(mrdhdf5_to_stream cpp/mrdhdf5_to_stream.cpp)
target_link_libraries(mrdhdf5_to_stream ISMRMRD::ISMRMRD fmt)
install(TARGETS mrdhdf5_to_stream DESTINATION bin)

add_executable(stream_to_mrdhdf5 cpp/stream_to_mrdhdf5.cpp)
target_link_libraries(stream_to_mrdhdf5 ISMRMRD::ISMRMRD fmt)
install(TARGETS stream_to_mrdhdf5 DESTINATION bin)
