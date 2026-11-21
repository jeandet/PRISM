# Headers only: the CMSIS core headers (CMSIS/Core/Include -- core_cm7.h and friends) that
# CMSIS-DSP's Arm build includes. There is no code to compile, so this exports just the include
# directory, which the cmsis-dsp packagefile adds on Arm through
# subproject('cmsis-core').get_variable('cmsis_core_inc'). The revision is the one prism-stm32's
# third_party/cmsis-core submodule pins.
project('cmsis-core', 'c')

cmsis_core_inc = include_directories('CMSIS/Core/Include')
