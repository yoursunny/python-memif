from distutils.core import Extension, setup

memifmodule = Extension('memif',
                        sources=['memifmodule.c'], libraries=['memif'], extra_compile_args=['-Wall', '-Werror'])

setup(name='memif',
      version='0.0',
      description='memif',
      ext_modules=[memifmodule])
