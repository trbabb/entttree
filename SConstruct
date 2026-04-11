import os

env = Environment(
    CXX='clang++',
    CXXFLAGS=[
        '-std=c++20',
        '-O2',
        '-Wall',
        '-Werror',
        '-fcolor-diagnostics',
    ],
    CPPPATH=[
        '#/include',
        '/usr/local/include',
        '/opt/homebrew/include',
    ],
    LIBPATH=[
        '/usr/local/lib',
        '/opt/homebrew/lib',
    ],
)

# library
lib_sources = Glob('src/*.cpp')
lib = env.StaticLibrary('theta-hierarchy', lib_sources)

# test
test_env = env.Clone()
test_env.Append(LIBS=[lib])
test_prog = test_env.Program('test/test_hierarchy', Glob('test/*.cpp'))
