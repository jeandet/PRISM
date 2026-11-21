# The shared board apps -- the widget demo (widgets_app.hpp) and the audio scope (audio_scope,
# spectrum, sample_source, synthetic_source) -- moved from prism-stm32, so every board
# repository builds the same sources (doc/design/esp32-port.md, "Shared apps move into PRISM").
# prism_embedded_apps_dep carries the include directory examples/ itself (the apps include each
# other as <embedded/audio_scope.hpp>), PRISM's core and CMSIS-DSP, whose arm_rfft_fast_f32 is
# the scope's FFT.
#
# Entered outside the sdl block in meson.build, so it runs on every target, bare metal ('none')
# included.
cmsis_dsp_dep = subproject('cmsis-dsp').get_variable('cmsis_dsp_dep')

# The apps' three .cpp files, as a static library (a board links it into its firmware).
# Dependencies, as prism-stm32's host scope_lib had them: PRISM's core (the widget demo and the
# scope build their views on it) and CMSIS-DSP (spectrum.cpp's FFT). audio_scope.cpp and
# widgets_app.hpp use frame_app.hpp only for FrameContext, whose methods are inline -- the one
# out-of-line symbol there, run_frames, is called by the board's main, which links
# prism_platform_dep itself -- so no platform library here. Nothing depends on SDL, so this
# builds on a bare-metal ('none') target too.
prism_embedded_apps_inc = include_directories('..')
prism_embedded_apps_lib = static_library('prism_embedded_apps',
  files('spectrum.cpp', 'synthetic_source.cpp', 'audio_scope.cpp'),
  include_directories : prism_embedded_apps_inc,
  dependencies : [prism_core_dep, cmsis_dsp_dep],
)

prism_embedded_apps_dep = declare_dependency(
  link_with : prism_embedded_apps_lib,
  include_directories : prism_embedded_apps_inc,
  dependencies : [prism_core_dep, cmsis_dsp_dep],
)
