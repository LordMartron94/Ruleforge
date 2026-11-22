#!/bin/sh
# General's $_semver is the <valid semver> grammar of Semantic Versioning 2.0.0: a probe grammar makes it one token, and the
# versions of the specification and of its public test suites are accepted or rejected as they say.
#   semver_test.sh <genesis> <stdlib folder>
set -e
genesis=$1
stdlib=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/SemverProbe.gen" <<'EOF'
Language: SemverProbe
Version: 1.0.0

IMPORT General { $_semver }

PARSER {
	entry = #document
}

CONTEXT <ROOT> {
	$version = $_semver()
}

#document = $version
EOF

failures=0

for version in 0.0.4 1.2.3 10.20.30 1.1.2-prerelease+meta 1.1.2+meta 1.1.2+meta-valid 1.0.0-alpha 1.0.0-alpha.beta \
    1.0.0-alpha.1 1.0.0-alpha0.valid 1.0.0-alpha.0valid 1.0.0-alpha-a.b-c-somethinglong+build.1-aef.1-its-okay \
    1.0.0-rc.1+build.1 10.2.3-DEV-SNAPSHOT 1.2.3-SNAPSHOT-123 2.0.0+build.1848 1.0.0-alpha+beta \
    1.2.3----RC-SNAPSHOT.12.9.1--.12+788 1.2.3----R-S.12.9.1--.12+meta 1.0.0+0.build.1-rc.10000aaa-kk-0.1 \
    99999999999999999999999.999999999999999999.99999999999999999 1.0.0-0A.is.legal 1.0.0+001 1.0.0-0 1.0.0-a-0 0.0.0; do
  printf '%s' "$version" > "$work/input.txt"
  if ! "$genesis" check "$work/SemverProbe.gen" "$work/input.txt" -I "$stdlib" -q >/dev/null 2>&1; then
    echo "FAIL: '$version' is a valid semantic version and was rejected" >&2
    failures=$((failures + 1))
  fi
done

# A leading zero in the core or in a numeric pre-release identifier, an empty identifier, a missing part, a "v" prefix,
# a second "+", whitespace and non-ASCII characters are not part of the grammar.
for version in 1 1.2 1.2.3.4 1.2.3-0123 1.2.3-0123.0123 1.1.2+.123 +invalid -invalid -invalid+invalid alpha 01.1.1 1.01.1 \
    1.1.01 1.2.3-01 1.2.3+ 1.2.3- 1.2.3-+ 1.2.3-a..b 1.2.3+a..b 1.2.3-a. 1.0.0-alpha_beta 9.8.7+meta+meta \
    9.8.7-whatever+meta+meta v1.2.3 '1.2.3 ' ' 1.2.3' 1.2.3-é 1.2.3+é; do
  printf '%s' "$version" > "$work/input.txt"
  if "$genesis" check "$work/SemverProbe.gen" "$work/input.txt" -I "$stdlib" -q >/dev/null 2>&1; then
    echo "FAIL: '$version' is not a valid semantic version and was accepted" >&2
    failures=$((failures + 1))
  fi
done

[ "$failures" -eq 0 ]
