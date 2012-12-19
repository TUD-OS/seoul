# -*- Mode: Python -*-

env = Environment(CPPPATH = ['#include', "#include-arch"],
                  LINKFLAGS = '-g ',
                  CCFLAGS = '-g -O2 -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter ',
                  CXXFLAGS = '-std=gnu++11 ')

AlwaysBuild(Command('version.inc', [], """( git describe --dirty --long --always || echo UNKNOWN ) | sed 's/^\\(.*\\)$/"\\1"/' > $TARGET"""))


seoul = env.Program('seoul',
                    ['main.cc',
                     'model/nullio.cc',
                     ] +
                    Glob('arch/*.cc'))

Default(seoul)

# EOF
