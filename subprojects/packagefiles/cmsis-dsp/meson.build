# Our own build file for CMSIS-DSP, tag v1.16.2 (the audio scope's FFT, arm_rfft_fast_f32):
# upstream builds with CMake. Only the eight files the scope needs are compiled, as a static
# library with upstream's warnings off (-w). On Arm the CMSIS core headers (core_cm7.h and
# friends) come from the cmsis-core subproject; on any other host __GNUC_PYTHON__ is CMSIS-DSP's
# own switch for a non-Arm GCC build (it skips cmsis_compiler.h) -- the portable C path, the
# same one prism-stm32's host build compiles.
project('cmsis-dsp', 'c', version : '1.16.2')

cmsis_dsp_incs = [include_directories('Include', 'PrivateInclude')]
cmsis_dsp_args = []
if host_machine.cpu_family() == 'arm' and host_machine.system() == 'none'   # a Cortex-M; a Linux ARM host takes the portable path
  cmsis_dsp_incs += subproject('cmsis-core').get_variable('cmsis_core_inc')
else
  cmsis_dsp_args += '-D__GNUC_PYTHON__'
endif

cmsis_dsp_lib = static_library('cmsis-dsp', files(
    'Source/TransformFunctions/arm_rfft_fast_f32.c',
    'Source/TransformFunctions/arm_rfft_fast_init_f32.c',
    'Source/TransformFunctions/arm_cfft_f32.c',
    'Source/TransformFunctions/arm_cfft_init_f32.c',
    'Source/TransformFunctions/arm_cfft_radix8_f32.c',
    'Source/TransformFunctions/arm_bitreversal2.c',
    'Source/CommonTables/arm_common_tables.c',
    'Source/CommonTables/arm_const_structs.c',
  ),
  include_directories : cmsis_dsp_incs,
  c_args : cmsis_dsp_args + ['-w'],
)

# Consumers (the scope's spectrum.cpp) include arm_math.h, so the dependency carries the include
# directories and the portable-path define too -- but not -w, which is only for upstream's files.
cmsis_dsp_dep = declare_dependency(
  link_with : cmsis_dsp_lib,
  include_directories : cmsis_dsp_incs,
  compile_args : cmsis_dsp_args,
)
