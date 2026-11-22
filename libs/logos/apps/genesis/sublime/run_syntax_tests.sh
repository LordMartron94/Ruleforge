#!/bin/sh
# Runs generated syntax tests with Sublime Text's own headless runner.
#
#   run_syntax_tests.sh <runner-dir> <syntax-file> <test-file>...
#
# <runner-dir> holds the unpacked "syntax_tests" build matching your Sublime Text (st_syntax_tests_build_<N>_x64.tar.xz
# from download.sublimetext.com). The syntax and the tests are copied into its Data/Packages/User; the exit code is
# the runner's (0 = all assertions hold).
set -e
runner=$1
syntax=$2
shift 2

packages="$runner/Data/Packages/User"
mkdir -p "$packages"
cp "$syntax" "$packages/"
for test in "$@"; do
  cp "$test" "$packages/"
done
cd "$runner"
./syntax_tests
