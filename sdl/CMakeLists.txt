# CMake build for the SDL 3 interface to Xconq (sdlconq).

set(SDLCONQ_SOURCES
    sdlunix.cc
    sdlmain.cc
    sdlinit.cc
    sdlcmd.cc
    sdlscreen.cc
    sdlmap.cc
    sdlimf.cc
    sdluact.cc
    sdlcltbl.cc)

add_executable(sdlconq ${SDLCONQ_SOURCES})
target_include_directories(sdlconq PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR})
target_link_libraries(sdlconq PRIVATE
    conq SDL3::SDL3 m)
if(SDL3_ttf_FOUND)
    target_link_libraries(sdlconq PRIVATE SDL3_ttf::SDL3_ttf)
endif()

install(TARGETS sdlconq RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
