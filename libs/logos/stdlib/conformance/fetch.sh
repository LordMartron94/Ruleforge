#!/bin/sh
# Fetches the official test suites the standard grammars are checked against into a folder (default: ./suites).
#   fetch.sh [DIR]
set -e
dir=${1:-suites}
mkdir -p "$dir"
clone() {
  [ -d "$dir/$1" ] || git clone --depth 1 -q "$2" "$dir/$1"
}
clone toml-test https://github.com/toml-lang/toml-test.git
clone JSONTestSuite https://github.com/nst/JSONTestSuite.git

# The W3C XML Conformance Test Suite (xmlconf).
if [ ! -d "$dir/xmlconf" ]; then
  curl -sL -o "$dir/xmlts.tgz" https://www.w3.org/XML/Test/xmlts20130923.tar.gz
  tar xzf "$dir/xmlts.tgz" -C "$dir"
  rm "$dir/xmlts.tgz"
fi
