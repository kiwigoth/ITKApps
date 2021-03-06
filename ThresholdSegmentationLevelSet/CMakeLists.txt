PROJECT( ThresholdSegmentationLevelSet )

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

  FIND_PACKAGE(VTK)
  IF (VTK_FOUND)
      INCLUDE (${VTK_USE_FILE})
  ENDIF (VTK_FOUND)

ENDIF( BUILD_OUTSIDE_INSIGHT_APPLICATIONS )


INCLUDE_DIRECTORIES(
${ITKApps_SOURCE_DIR}/Auxiliary/FltkImageViewer
${ITKApps_SOURCE_DIR}/Auxiliary/VtkFltk
${ITKApps_SOURCE_DIR}/Auxiliary/vtk
${ITKApps_BINARY_DIR}/Auxiliary/FltkImageViewer
${ITKApps_BINARY_DIR}/Auxiliary/VtkFltk
${ThresholdSegmentationLevelSet_SOURCE_DIR}
${ThresholdSegmentationLevelSet_BINARY_DIR}
)

SET(ThresholdSegmentationLevelSet_GUI_SRCS
ThresholdSegmentationLevelSetGUI.fl
)

FLTK_WRAP_UI( ThresholdSegmentationLevelSet ${ThresholdSegmentationLevelSet_GUI_SRCS} )
ITK_DISABLE_FLTK_GENERATED_WARNINGS("${ThresholdSegmentationLevelSet_GUI_SRCS}")

SET(ThresholdSegmentationLevelSet_SRCS
ThresholdSegmentationLevelSetBase.cxx
ThresholdSegmentationLevelSet.cxx
${ThresholdSegmentationLevelSet_FLTK_UI_SRCS}
)

ADD_GUI_EXECUTABLE(ThresholdSegmentationLevelSet "${ThresholdSegmentationLevelSet_SRCS}")

TARGET_LINK_LIBRARIES (ThresholdSegmentationLevelSet
  ${ITK_LIBRARIES}
  ITKFltkImageViewer
  ITKVtkFltk
  ${VTK_LIBRARIES}
)

IF(VTK_USE_VOLUMERENDERING)
 TARGET_LINK_LIBRARIES(ThresholdSegmentationLevelSet
 vtkVolumeRendering
 )
ENDIF(VTK_USE_VOLUMERENDERING)

IF(CMAKE_COMPILER_IS_GNUCXX)
  SET_SOURCE_FILES_PROPERTIES (ThresholdSegmentationLevelSet.cxx PROPERTIES COMPILE_FLAGS -Wno-cast-qual)
ENDIF(CMAKE_COMPILER_IS_GNUCXX)
