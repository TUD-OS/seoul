# -*- Mode: Python -*-

env = Environment(CPPPATH = ['#include', "#include-arch"],
                  CCFLAGS = '-Wall -Wextra ',
                  CXXFLAGS = '-std=gnu++11 ')

seoul = env.Program('seoul', ['main.cc',
                              'model/nullio.cc'])

Default(seoul)

# EOF
