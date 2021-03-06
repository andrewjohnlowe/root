############################################################################
# CMakeLists.txt file for building ROOT gui/cefdisplay package
############################################################################

if(DEFINED ENV{CEF_ROOT})

set(libname ROOTCefDisplay)

set(CEF_sources src/simple_app.cxx src/base_handler.cxx src/osr_handler.cxx src/gui_handler.cxx)

set(CEF_root $ENV{CEF_ROOT})

if(ROOT_ARCHITECTURE MATCHES macosx)
   set(CEF_platform src/gui_handler_mac.mm)
   set(CEF_RESOURCES $ENV{CEF_ROOT}/Release/Chromium\ Embedded\ Framework.framework/Resources)
   set(CEF_BLOBS ${CEF_RESOURCES})
   set(CEF_LIBRARY ${CEF_root}/Release/Chromium\ Embedded\ Framework.framework/Chromium\ Embedded\ Framework)
   set(CEF_LIB_DEPENDENCY "-framework Foundation")
   set(CEF_DLL_WRAPPER ${CEF_root}/build/libcef_dll_wrapper/libcef_dll_wrapper.a)
elseif(MSVC)
   set(CEF_platform src/gui_handler_win.cc)
   set(CEF_RESOURCES ${CEF_root}/Resources)
   set(CEF_BLOBS ${CEF_root}/Release)
   set(CEF_LIBRARY ${CEF_root}/Release/libcef.lib)
   set(CEF_DLL_WRAPPER ${CEF_root}/build/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib)
   configure_file(${CEF_root}/Release/libcef.dll ${CMAKE_BINARY_DIR}/bin/ COPYONLY)
else ()
   set(CEF_platform src/gui_handler_linux.cxx)
   set(CEF_RESOURCES ${CEF_root}/Resources)
   set(CEF_BLOBS ${CEF_root}/Release)
   set(CEF_LIBRARY ${CEF_root}/Release/libcef.so)
   set(CEF_DLL_WRAPPER ${CEF_root}/build/libcef_dll_wrapper/libcef_dll_wrapper.a)
   set(CEF_LIB_DEPENDENCY ${X11_LIBRARIES})
endif()

include_directories($ENV{CEF_ROOT} inc)
set(CEF_MAIN src/cef_main.cxx)


## copy different CEF files into binary directory to avoid CEF_ROOT when running ROOT

file(GLOB cef_bins ${CEF_BLOBS}/*.bin)

foreach(cef_b ${cef_bins})
  configure_file(${cef_b} ${CMAKE_BINARY_DIR}/bin/ COPYONLY)
endforeach()

file(GLOB_RECURSE cef_resources RELATIVE ${CEF_RESOURCES} ${CEF_RESOURCES}/*)

foreach(cef_r ${cef_resources})
  configure_file(${CEF_RESOURCES}/${cef_r} ${CMAKE_BINARY_DIR}/bin/${cef_r} COPYONLY)
endforeach()



ROOT_LINKER_LIBRARY(${libname} ${CEF_sources} ${CEF_platform}
                    LIBRARIES ${CMAKE_DL_LIBS} ${CEF_LIBRARY} ${CEF_DLL_WRAPPER} ${CEF_LIB_DEPENDENCY}
                    DEPENDENCIES RHTTP ROOTWebDisplay)

ROOT_EXECUTABLE(cef_main ${CEF_MAIN} LIBRARIES ${CEF_LIBRARY} ${CEF_DLL_WRAPPER})

endif()