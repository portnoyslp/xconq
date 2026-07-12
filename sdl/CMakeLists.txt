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

# Package sdlconq as a real, relocatable .app bundle on macOS. Off by
# default elsewhere; can be turned off even on macOS as an escape hatch
# back to a flat binary, without a code change.
option(XCONQ_MACOS_BUNDLE "Package sdlconq as a macOS .app bundle" ${APPLE})

if(APPLE AND XCONQ_MACOS_BUNDLE)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
                   ${CMAKE_CURRENT_BINARY_DIR}/Info.plist)
    target_sources(sdlconq PRIVATE Xconq.icns)
    set_source_files_properties(Xconq.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(sdlconq PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
        MACOSX_BUNDLE_ICON_FILE Xconq.icns)
    # Real copies, not symlinks, so the bundle is self-contained.
    add_custom_command(TARGET sdlconq POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/lib
                $<TARGET_BUNDLE_CONTENT_DIR:sdlconq>/Resources/lib
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/images
                $<TARGET_BUNDLE_CONTENT_DIR:sdlconq>/Resources/images)
endif()

install(TARGETS sdlconq RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                        BUNDLE DESTINATION .)
