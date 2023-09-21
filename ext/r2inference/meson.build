gstinference_sources = [
	'gstinceptionv1.c',
	'gstinceptionv2.c',
	'gstinceptionv3.c',
	'gstinceptionv4.c',
	'gstinference.c',
	'gstmobilenetv2.c',
	'gstmobilenetv2ssd.c',
	'gstresnet50v1.c',
	'gsttinyyolov2.c',
	'gsttinyyolov3.c',
	'gstrosetta.c'
]

gstinference = library('gstinference',
  gstinference_sources,
  c_args : c_args,
  include_directories : [configinc, inference_inc_dir],
  dependencies : [gst_base_dep, gst_dep, gobject_dep, glib_dep, gst_video_dep, tensorrt_dep, cuda_dep, cudart_dep, gmodule_dep, gstinference_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstinference]
