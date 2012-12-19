# -*- Mode: Python -*-

env = Environment(CPPPATH = ['#include', "#include-arch"],
                  LINKFLAGS = '-g ',
                  CCFLAGS = '-g -O2 -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter ',
                  CXXFLAGS = '-std=gnu++11 ')

seoul = env.Program('seoul',
                    ['main.cc',
                     'model/nullio.cc',
                     ] +
                    Glob('arch/*.cc'))

Default(seoul)

# EOF
