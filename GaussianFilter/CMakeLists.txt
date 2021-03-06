PROJECT(GaussianFilter)


IF( BUILD_OUTSIDE_INSIGHT_APPLICATIONS )

  FIND_PACKAGE(ITK)
  IF(ITK_FOUND)
    INCLUDE(${ITK_USE_FILE})
  ELSE(ITK_FOUND)
    MESSAGE(FATAL_ERROR
            "Cannot build ITKApps without ITK.  Please set ITK_DIR.")
  ENDIF(ITK_FOUND)

  FIND_PACKAGE(FLTK)
  IF(FLTK_FOUND)
    INCLUDE_DIRECTORIES(${FLTK_INCLUDE_DIR})
  ENDIF(FLTK_FOUND)

ENDIF( BUILD_OUTSIDE_INSIGHT_APPLICATIONS )



INCLUDE_DIRECTORIES(
  ${ITKApps_SOURCE_DIR}/Auxiliary/FltkImageViewer
  ${ITKApps_BINARY_DIR}/Auxiliary/FltkImageViewer
  ${ITKApps_SOURCE_DIR}/GaussianFilter
  ${ITKApps_BINARY_DIR}/GaussianFilter
)

SET(GaussianFilter_SRCS
  liFilterConsole.cxx
  liFilterConsoleBase.cxx
  GaussianFilter.cxx
  )

SET(GaussianFilter2D_SRCS
  liFilterConsole2D.cxx
  liFilterConsole2DBase.cxx
  GaussianFilter2D.cxx
  )

FLTK_WRAP_UI(GaussianFilter liFilterConsoleGUI.fl)
FLTK_WRAP_UI(GaussianFilter2D liFilterConsole2DGUI.fl)

SET(GaussianFilter_SRCS
${GaussianFilter_SRCS}
${GaussianFilter_FLTK_UI_SRCS}
)

SET(GaussianFilter2D_SRCS
${GaussianFilter2D_SRCS}
${GaussianFilter2D_FLTK_UI_SRCS}
)

ADD_GUI_EXECUTABLE(GaussianFilter   "${GaussianFilter_SRCS}")
ADD_GUI_EXECUTABLE(GaussianFilter2D "${GaussianFilter2D_SRCS}")

ITK_DISABLE_FLTK_GENERATED_WARNINGS(GaussianFilter.cxx)
ITK_DISABLE_FLTK_GENERATED_WARNINGS(liFilterConsole.cxx)
ITK_DISABLE_FLTK_GENERATED_WARNINGS(liFilterConsoleGUI.cxx)
ITK_DISABLE_FLTK_GENERATED_WARNINGS(liFilterConsole2D.cxx)
ITK_DISABLE_FLTK_GENERATED_WARNINGS(liFilterConsole2DGUI.cxx)

TARGET_LINK_LIBRARIES(GaussianFilter ITKFltkImageViewer ${ITK_LIBRARIES})
TARGET_LINK_LIBRARIES(GaussianFilter2D ITKFltkImageViewer ${ITK_LIBRARIES})
