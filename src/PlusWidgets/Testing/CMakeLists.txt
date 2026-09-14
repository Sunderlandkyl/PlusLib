SET( TestDataDir ${PLUSLIB_DATA_DIR}/TestImages )
SET( ConfigFilesDir ${PLUSLIB_DATA_DIR}/ConfigFiles )

# --------------------------------------------------------------------------
# ToolStateDisplayWidgetTest
SET(ToolStateDisplayWidgetTest_LIBS 
  Qt${QT_VERSION_MAJOR}::Widgets
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Test
  PlusWidgets
  vtkPlusCommon
  vtkPlusDataCollection
  )

ADD_EXECUTABLE(ToolStateDisplayWidgetTest ToolStateDisplayWidgetTest.cxx 
  ${CMAKE_CURRENT_SOURCE_DIR}/../PlusWidgets.qrc 
  )
SET_TARGET_PROPERTIES(ToolStateDisplayWidgetTest PROPERTIES 
  FOLDER Tests
  )
target_include_directories(ToolStateDisplayWidgetTest PRIVATE 
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  )
TARGET_LINK_LIBRARIES( ToolStateDisplayWidgetTest PRIVATE ${ToolStateDisplayWidgetTest_LIBS} )

#--------------------------------------------------------------------------------------------
ADD_TEST(ToolStateDisplayWidgetTest ${PLUS_EXECUTABLE_OUTPUT_PATH}/ToolStateDisplayWidgetTest)
SET_TESTS_PROPERTIES( ToolStateDisplayWidgetTest PROPERTIES FAIL_REGULAR_EXPRESSION "ERROR;WARNING" )