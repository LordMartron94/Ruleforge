#!/usr/bin/env python3
"""Runs a standard grammar over a language's official test suite and reports where it disagrees with the suite.

  run.py GENESIS GRAMMAR SUITE_DIR --accept GLOB --reject GLOB [--list FILE --accept-prefix valid --reject-prefix invalid]
         [--expect-unrejected FILE] [-I DIR]...

A file matched by an accept pattern must parse, one matched by a reject pattern must not. With --list, SUITE_DIR-relative
paths are read from FILE (a suite that names the files of each language version), and the accept/reject patterns are
prefixes of those paths. --expect-unrejected names a file of paths the grammar is known to accept although the suite rejects
them, because the rule they break is a semantic constraint (a duplicate key, a mismatched end tag) and not a syntax rule; the
run fails if the grammar rejects one of them too (the list is stale) or accepts a reject file that is not on it.
Exit status 0 when nothing else disagrees.
"""
import argparse
import concurrent.futures as futures
import glob
import os
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("genesis")
    parser.add_argument("grammar")
    parser.add_argument("suite")
    parser.add_argument("--accept", required=True)
    parser.add_argument("--reject", required=True)
    parser.add_argument("--list")
    parser.add_argument("--list-script", help="a script that prints the list for SUITE_DIR (xmlconf_list.py)")
    parser.add_argument("--suffix", default="")
    parser.add_argument("--strip-prefix", action="store_true", help="the first component of a listed path says accept or reject and is not part of the path")
    parser.add_argument("--expect-unrejected")
    parser.add_argument("-I", action="append", default=[])
    parser.add_argument("-n", type=int, default=20, help="how many disagreements to print per kind")
    args = parser.parse_args()

    include = [part for folder in args.I for part in ("-I", folder)]

    if args.list_script:
        lines = subprocess.run([sys.executable, args.list_script, args.suite], capture_output=True, text=True, check=True).stdout.splitlines()
        names = [line.strip() for line in lines if line.strip().endswith(args.suffix)]
        accept = [n for n in names if n.startswith(args.accept)]
        reject = [n for n in names if n.startswith(args.reject)]
    elif args.list:
        names = [line.strip() for line in open(args.list) if line.strip().endswith(args.suffix)]
        accept = [n for n in names if n.startswith(args.accept)]
        reject = [n for n in names if n.startswith(args.reject)]
    else:
        accept = [os.path.relpath(p, args.suite) for p in sorted(glob.glob(os.path.join(args.suite, args.accept), recursive=True))]
        reject = [os.path.relpath(p, args.suite) for p in sorted(glob.glob(os.path.join(args.suite, args.reject), recursive=True))]

    def parses(name):
        path = name.split("/", 1)[1] if args.strip_prefix else name

        return subprocess.run([args.genesis, "check", args.grammar, os.path.join(args.suite, path), "-q"] + include, capture_output=True).returncode == 0

    with futures.ThreadPoolExecutor(8) as pool:
        accepted = list(pool.map(parses, accept))
        rejected = list(pool.map(parses, reject))

    expected = set()

    if args.expect_unrejected:
        expected = {line.strip() for line in open(args.expect_unrejected) if line.strip() and not line.startswith("#")}

    wrongly_rejected = [n for n, ok in zip(accept, accepted) if not ok]
    wrongly_accepted = [n for n, ok in zip(reject, rejected) if ok and n not in expected]
    stale = [n for n, ok in zip(reject, rejected) if not ok and n in expected]
    known = [n for n, ok in zip(reject, rejected) if ok and n in expected]
    print(f"accept {len(accept) - len(wrongly_rejected)}/{len(accept)}   reject {len(reject) - len(wrongly_accepted) - len(known)}/{len(reject)}"
          f"   semantic (known, accepted): {len(known)}")

    for label, items in (("SHOULD ACCEPT", wrongly_rejected), ("SHOULD REJECT", wrongly_accepted), ("STALE EXPECTATION", stale)):
        for name in items[:args.n]:
            print(label, name)

    return 0 if not (wrongly_rejected or wrongly_accepted or stale) else 1


sys.exit(main())
