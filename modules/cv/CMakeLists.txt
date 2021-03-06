cmake_minimum_required(VERSION 2.8.2)

project(cv)

INCLUDE_DIRECTORIES(./include ../cxcore/include ../ts/include ../highgui/include)

add_library(cv SHARED
	src/imgwarp.cpp
	src/morph.cpp
	src/templmatch.cpp
	src/deriv.cpp
	src/filter.cpp
	src/distransform.cpp
	src/accum.cpp
	src/avx/imgwarp_avx.cpp
	src/avx2/imgwarp_avx2.cpp
	src/tables.cpp
	src/color.cpp
	src/smooth.cpp
	src/thresh.cpp
	src/utils.cpp
	src/generalized_hough.cpp
	src/canny.cpp

	src/grabcut.cpp
	src/phasecorr.cpp
	src/floodfill.cpp
	src/pyramids.cpp
	src/cornersubpix.cpp
	src/samplers.cpp
	src/subdivision2d.cpp
	src/segmentation.cpp
	src/clahe.cpp
	src/linefit.cpp
	src/sumpixels.cpp
	src/histogram.cpp
	src/emd.cpp
	src/gabor.cpp
	src/featureselect.cpp
	src/corner.cpp
	src/undistort.cpp
	src/hough.cpp

	# src/rotcalipers.cpp
	# src/shapedescr.cpp
	# src/moments.cpp
	# src/contours.cpp
	# src/convhull.cpp
	# src/geometry.cpp
	# src/approx.cpp
	# src/matchcontours.cpp
	)

add_executable(test_cv
	# test/test_moments.cpp
	# test/test_convhull.cpp
	# test/test_distancetransform.cpp
	# test/test_approxpoly.cpp
	# test/test_cvtyuv.cpp
	# test/test_floodfill.cpp
	# test/test_filter.cpp
	test/test_color.cpp
	# test/test_thresh.cpp
	# test/test_pc.cpp
	# test/test_bilateral_filter.cpp
	# test/test_templmatch.cpp
	# test/test_canny.cpp
	# test/test_boundingrect.cpp
	# test/test_grabcut.cpp
	# test/test_houghLines.cpp
	# test/test_contours.cpp
	# test/test_imgwarp.cpp
	# test/test_emd.cpp
	# test/test_watershed.cpp
	# test/test_histograms.cpp
	test/test_main.cpp
	# test/test_imgwarp_strict.cpp
	)
target_link_libraries(test_cv cxcore ts cv)

#---------------------------------------------------------------------
# Find OpenMP
find_package( OpenMP )

if( OPENMP_FOUND )
  set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}" )
  set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}" )
	IF(WIN32)
		TARGET_LINK_LIBRARIES(cv cxcore)
	ELSEIF(APPLE)
		TARGET_LINK_LIBRARIES(cv pthread cxcore iomp5)
	ELSE(WIN32)
		TARGET_LINK_LIBRARIES(cv pthread rt cxcore gomp)
	ENDIF(WIN32)
ELSE (OPENMP_FOUND)
	IF(WIN32)
	ELSEIF(APPLE)
		TARGET_LINK_LIBRARIES(cv pthread cxcore)
	ELSE(WIN32)
		TARGET_LINK_LIBRARIES(cv pthread rt cxcore)
	ENDIF(WIN32)
ENDIF(OPENMP_FOUND)

