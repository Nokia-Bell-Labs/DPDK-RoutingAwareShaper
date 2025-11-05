# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2017-2019 Intel Corporation
#
# LC - 02/26/2021: Added new DaaS/PoCPhase2 (abbreviated as opocp2) directory.
# AF - 10/28/2025: The option name (opocp2) remains the same, but it now refers
# 	      	   	   to the DaaS/PoCPhase3 directory
#
#              © 2020-2025 Nokia
#              Licensed under the BSD 3-Clause Clear License
#              SPDX-License-Identifier: BSD-3-Clause-Clear
#
link_whole_libs = []
if get_option('default_library') == 'static'
	link_whole_libs = dpdk_static_libraries + dpdk_drivers
endif

execinfo = cc.find_library('execinfo', required: false)

# list of all Orion PoCPhase2 apps. Keep 1-3 per line, in alphabetical order.
all_opocp2 = [
	'tm10'
]

# install all Orion PoCPhase2 code on install - irrespective of whether the code in
# question is to be built as part of this build or not.
foreach ex:all_opocp2
	install_subdir(ex,
			install_dir: get_option('datadir') + '/dpdk/opocp2',
			exclude_files: 'meson.build')
endforeach

if get_option('opocp2') == ''
	subdir_done()
endif

if get_option('opocp2').to_lower() == 'all'
	opocp2 = all_opocp2
	allow_skips = true # don't flag an error if we can't build an app
else
	opocp2 = get_option('opocp2').split(',')
	allow_skips = false # error out if we can't build a requested app
endif
default_cflags = machine_args
if cc.has_argument('-Wno-format-truncation')
	default_cflags += '-Wno-format-truncation'
endif

foreach xpocp2: opocp2
	name = xpocp2.split('/')[-1]
	build = true
	sources = []
	allow_experimental_apis = false
	cflags = default_cflags

	ext_deps = [execinfo]
	includes = [include_directories(xpocp2)]
	deps = ['eal', 'mempool', 'net', 'mbuf', 'ethdev', 'cmdline']
	if is_windows
		deps = ['eal'] # only supported lib on Windows currently
	endif
	subdir(xpocp2)

	if build
		dep_objs = ext_deps
		foreach d:deps
			var_name = get_option('default_library') + '_rte_' + d
			if not is_variable(var_name)
				error('Missing dependency "@0@" for xpocp2 "@1@"'.format(d, name))
			endif
			dep_objs += [get_variable(var_name)]
		endforeach
		if allow_experimental_apis
			cflags += '-DALLOW_EXPERIMENTAL_API'
		endif
		executable('dpdk-' + name, sources,
			include_directories: includes,
			link_whole: link_whole_libs,
			link_args: dpdk_extra_ldflags,
			c_args: cflags,
			dependencies: dep_objs)
	elif not allow_skips
		error('Cannot build requested xpocp2 "' + name + '"')
	else
		message('Skipping xpocp2 "' + name + '"')
	endif
endforeach
