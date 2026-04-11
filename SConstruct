import os

build_dir = 'build'

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

# emit compile_commands.json
env.Tool('compilation_db')
cdb = env.CompilationDatabase('compile_commands.json')
Alias('compdb', cdb)

# build objects in build/ directory
lib_obj  = env.Object(
    [env.File(f'build/src/{os.path.basename(str(s)).replace(".cpp", ".o")}')
     for s in Glob('src/*.cpp')],
    Glob('src/*.cpp')
)
lib = env.StaticLibrary(f'{build_dir}/libentttree', lib_obj)

test_obj = env.Object(f'{build_dir}/test/test_hierarchy.o', 'test/test_hierarchy.cpp')
test_prog = env.Program(
    f'{build_dir}/test/test_hierarchy',
    test_obj,
    LIBS=[lib],
)
Alias('test', test_prog)
Default([lib, test_prog, cdb])
