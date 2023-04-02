#!/bin/sh 

find . -type f -iname '*.h' -o -iname '*.cpp' -o -iname '*.c' | xargs clang-format -i --style=LLVM --verbose
