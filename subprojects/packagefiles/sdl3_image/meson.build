# Our own build of SDL3_image: WrapDB's port leaves out IMG.c (the generic loader) and defines no
# format, so every loader compiles to a stub. Formats come from SDL_image's bundled decoders
# (stb_image for PNG/JPEG, nanosvg for SVG): no libpng or libjpeg, the same on every platform.
# Static and position-independent: links into the backend library, no DLL exports on Windows.
project('sdl3_image', 'c', version : '3.2.4')

sdl_dep = dependency('sdl3', version : '>=3.2.4')

formats = ['BMP', 'GIF', 'JPG', 'PNG', 'QOI', 'SVG', 'TGA']
# COMMON_BACKEND: these decoders on Apple too (not ImageIO), so every platform decodes alike.
img_args = ['-DUSE_STBIMAGE', '-DSDL_IMAGE_USE_COMMON_BACKEND', '-DSDL_IMAGE_SAVE_PNG=0', '-DSDL_IMAGE_SAVE_JPG=0']
foreach f : formats
  img_args += '-DLOAD_' + f
endforeach

# IMG.c's format table names every format's functions; the ones not defined above compile to stubs.
img_src = files(
  'src/IMG.c', 'src/IMG_avif.c', 'src/IMG_bmp.c', 'src/IMG_gif.c', 'src/IMG_jpg.c',
  'src/IMG_jxl.c', 'src/IMG_lbm.c', 'src/IMG_pcx.c', 'src/IMG_png.c', 'src/IMG_pnm.c',
  'src/IMG_qoi.c', 'src/IMG_stb.c', 'src/IMG_svg.c', 'src/IMG_tga.c', 'src/IMG_tif.c',
  'src/IMG_webp.c', 'src/IMG_WIC.c', 'src/IMG_xcf.c', 'src/IMG_xpm.c', 'src/IMG_xv.c',
  'src/IMG_xxx.c',
)

img_lib = static_library('sdl3_image', img_src,
  c_args : img_args,
  include_directories : 'include',
  dependencies : sdl_dep,
  pic : true)

sdl3_image_dep = declare_dependency(include_directories : 'include', link_with : img_lib)
meson.override_dependency('sdl3_image', sdl3_image_dep)
