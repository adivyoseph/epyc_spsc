project('spsc','c')

add_global_arguments('-O2', language : 'c')

src_files = ['main.c']

thread_dep = dependency('threads')

executable('spsc', sources : src_files ,dependencies : thread_dep)
