# -*- Mode: Python -*-

env = Environment(CPPPATH = ['#include', "#include-arch"],
                  LINKFLAGS = '-g ',
                  CCFLAGS = '-g -O2 -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter ',
                  CXXFLAGS = '-std=gnu++11 ')

# Execute git describe somewhere where our code is. This is useful,
# when this SConscript is used by third-party scons build systems
# directly.
AlwaysBuild(Command('version.inc', ['main.cc'], """( cd `dirname $SOURCE` && git describe --dirty || echo UNKNOWN ) | sed 's/^\\(.*\\)$/"\\1"/' > $TARGET"""))


seoul = env.Program('seoul',
                    ['main.cc',
                     'model/nullio.cc',
                     ] +
                    Glob('arch/*.cc'))

Default(seoul)

# EOF
