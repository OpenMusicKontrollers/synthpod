bndl_srcs = ['synthpod_bundle.c']

bndl = shared_module('synthpod_bundle', bndl_srcs,
	c_args : c_args,
	name_prefix : '',
	dependencies : [m_dep, lv2_dep],
	install : true,
	install_dir : bndl_dir)

configure_file(input : 'manifest.ttl.in', output : 'manifest.ttl',
	configuration : conf_data,
	install : true,
	install_dir : bndl_dir)

custom_target('synthpod_bundle_ttl',
	input : 'synthpod_bundle.ttl',
	output : 'synthpod_bundle.ttl',
	command : clone,
	install : true,
	install_dir : bndl_dir)