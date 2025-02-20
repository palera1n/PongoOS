# Declare project
# Bump version whenever tagged
project('PongoOS', 'c', 'cpp',
  version : '0.12.4',
  default_options : ['warning_level=0'])

# Force usage of clang
cc = meson.get_compiler('c')
if cc.get_id() != 'clang'
  error('Only compiling with clang is supported.')
endif

# Include header files
inc = include_directories(
  '/usr/aarch64-none-darwin/include',
  'src/apple-include',
  'src/include',
  'src/pongo/boot',
  'src/pongo/drivers',  
  'src/pongo/kernel',
  'src/pongo/lib',
)

# Declare dependencies (ORDER ALPHABETICALLY)
newlib = dependency('newlib-aarch64-darwin', static : true)

# List subdirectories
subdir('src')

pongo_sources = [
  pongo_sources_c,
  pongo_sources_asm,
]

# Generate executable
exe = executable('PongoOS', pongo_sources,
  dependencies : [newlib],
  include_directories : inc,
  c_args : [
    '-Os',
    '--target=arm64-apple-ios12.0',
    '-std=gnu17',
    '-moutline',
    '-flto',    
    '-ffreestanding',
    '-nostdlibinc',
    '-fno-blocks',
    '-U__nonnull',
    '-DTARGET_OS_OSX=0',
    '-DTARGET_OS_MACCATALYST=0',
    '-D_GNU_SOURCE',
    '-D__DYNAMIC_REENT__',
    '-DDER_TAG_SIZE=8',
    '-DPONGO_PRIVATE',
    '-DPONGO_VERSION="' + meson.project_version() + '"',
  ],
  link_args : [
    '-static',
    '-nostdlib',
    '-dead_strip',
    # '-preload',
    # '-no_uuid'
    '-Wl,-e,start',
    '-Wl,-order_file,../src/pongo/sym_order.txt',
    # '-Wl,-image_base,0x100000000',
    # '-Wl,-sectalign,__DATA,__common,0x8',
    # '-Wl,-segalign,0x4000',
    '-L /usr/aarch64-none-darwin/fixup',
    '-lc',
  ]
)
