gstinference_sources = [
	'gstbasebackend.cc',
	'gstchildinspector.c',
	'gstinferencebackend.cc',
	'gstinferencebackends.cc',
	'gstinferencedebug.c',
	'gstinferenceclassification.c',
	'gstinferencemeta.c',
	'gstinferenceprediction.c',
	'gstinferencepostprocess.c',
	'gstinferencepreprocess.c',
	'gstvideoinference.c'
]

gstinference_headers = [
	'gstbasebackend.h',
	'gstbasebackendsubclass.h',
	'gstchildinspector.h',
	'gstinferencebackends.h',
	'gstinferencedebug.h',
	'gstinferencemeta.h',
	'gstinferencepostprocess.h',
	'gstinferencepreprocess.h',
	'gstinferenceclassification.h',
	'gstinferenceprediction.h',
	'gstvideoinference.h'
]

if r2inference_dep.found()
  gstinference = static_library('gstinference-1.0',
    gstinference_sources,
    c_args : c_args,
    include_directories : [configinc, inference_inc_dir],
    #version : version_arr[0],
    install : true,
	install_dir : lib_install_dir,
    dependencies : [gst_base_dep, gst_video_dep, r2inference_dep],
  )

  gstinference_dep = declare_dependency(link_with: gstinference,
    include_directories : [inference_inc_dir],
    dependencies : [gst_video_dep, r2inference_dep])

  install_headers(gstinference_headers, subdir : 'gstreamer-1.0/gst/r2inference')
  pkgconfig.generate(gstinference, install_dir : plugins_pkgconfig_install_dir)
else
  error('r2inference not found. Please install before proceeding with the installation.')
endif