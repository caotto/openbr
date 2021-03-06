set(BR_INSTALL_DEPENDENCIES OFF CACHE BOOL "Install runtime dependencies.")

# OpenCV Libs
function(install_opencv_library lib)
  if(${BR_INSTALL_DEPENDENCIES})
    if(CMAKE_HOST_WIN32)
      install(FILES ${OpenCV_DIR}/bin/Release/opencv_${lib}${OpenCV_VERSION_MAJOR}${OpenCV_VERSION_MINOR}${OpenCV_VERSION_PATCH}.dll DESTINATION bin)
    elseif(CMAKE_HOST_APPLE)
      set(OpenCV_LIB_DIR "/usr/local/lib")
      install(FILES ${OpenCV_LIB_DIR}/libopencv_${lib}.${OpenCV_VERSION_MAJOR}.${OpenCV_VERSION_MINOR}.${OpenCV_VERSION_PATCH}.dylib DESTINATION lib)
      install(FILES ${OpenCV_LIB_DIR}/libopencv_${lib}.${OpenCV_VERSION_MAJOR}.${OpenCV_VERSION_MINOR}.dylib DESTINATION lib)
      install(FILES ${OpenCV_LIB_DIR}/libopencv_${lib}.dylib DESTINATION lib)
    else()
      set(OpenCV_LIB_DIR "/usr/local/lib")
      install(FILES ${OpenCV_LIB_DIR}/libopencv_${lib}.so.${OpenCV_VERSION_MAJOR}.${OpenCV_VERSION_MINOR}.${OpenCV_VERSION_PATCH} DESTINATION lib)
      install(FILES ${OpenCV_LIB_DIR}/libopencv_${lib}.so.${OpenCV_VERSION_MAJOR}.${OpenCV_VERSION_MINOR} DESTINATION lib)
      install(FILES ${OpenCV_LIB_DIR}/libopencv_${lib}.so DESTINATION lib)
    endif()
  endif()
endfunction()

function(install_opencv_libraries libs)
  foreach(lib ${${libs}})
    install_opencv_library(${lib})
  endforeach()
endfunction()

# Qt Libs
function(install_qt_library lib)
  if(${BR_INSTALL_DEPENDENCIES})
    if(CMAKE_HOST_WIN32)
      install(FILES ${QT_LIBRARY_DIR}/../bin/${lib}${QT_VERSION_MAJOR}.dll DESTINATION bin)
    elseif(CMAKE_HOST_APPLE)
      install(DIRECTORY ${QT_LIBRARY_DIR}/${lib}.framework DESTINATION lib)
    else()
      install(FILES ${QT_LIBRARY_DIR}/lib${lib}.so.${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}.${QT_VERSION_PATCH} DESTINATION lib)
      install(FILES ${QT_LIBRARY_DIR}/lib${lib}.so.${QT_VERSION_MAJOR}.${QT_VERSION_MINOR} DESTINATION lib)
      install(FILES ${QT_LIBRARY_DIR}/lib${lib}.so.${QT_VERSION_MAJOR} DESTINATION lib)
      install(FILES ${QT_LIBRARY_DIR}/lib${lib}.so DESTINATION lib)
    endif()
  endif()
endfunction()

function(install_qt_libraries libs)
  foreach(lib ${${libs}})
    install_qt_library(${lib})
  endforeach()
endfunction()

# Qt Plugins
function(install_qt_imageformats)
  if(${BR_INSTALL_DEPENDENCIES})
    install(FILES ${QT_QGIF_PLUGIN_RELEASE}
                  ${QT_QICO_PLUGIN_RELEASE}
                  ${QT_QJPEG_PLUGIN_RELEASE}
                  ${QT_QMNG_PLUGIN_RELEASE}
                  ${QT_QSVG_PLUGIN_RELEASE}
                  ${QT_QTIFF_PLUGIN_RELEASE}
            DESTINATION bin/imageformats)
  endif()
endfunction()

# Compiler libraries
function(install_compiler_libraries)
  include(InstallRequiredSystemLibraries)
  if(${BR_INSTALL_DEPENDENCIES} AND MINGW)
    set(MINGW_DIR "MINGW_DIR-NOTFOUND" CACHE PATH "MinGW Path")
    get_filename_component(MINGW_DIR ${CMAKE_CXX_COMPILER} PATH)
    if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
      install(FILES ${MINGW_DIR}/libgcc_s_sjlj-1.dll DESTINATION bin)
    else()
      install(FILES ${MINGW_DIR}/libgcc_s_dw2-1.dll ${MINGW_DIR}/libstdc++-6.dll DESTINATION bin)
    endif()
  endif()
endfunction()

# R runtime
function(install_r_runtime)
  if(WIN32)
    find_path(R_DIR bin/Rscript.exe "C:/Program Files/R/*")
    install(DIRECTORY ${R_DIR}/ DESTINATION R)
  endif()
endfunction()
