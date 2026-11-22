#!/usr/bin/env python3
"""Lists the files of the W3C XML Conformance Test Suite that apply to XML 1.0 (Fifth Edition), for run.py --list.

  xmlconf_list.py XMLCONF_DIR > list

Each line is "accept/<path>" for a well-formed document (types valid and invalid: a non-validating parser accepts both) or
"reject/<path>" for a not-well-formed one (type not-wf), the path relative to XMLCONF_DIR. Tests that need a feature outside
the grammar are left out: other XML versions and editions, documents in encodings other than UTF-8 (they are transcoded
before parsing) and tests of external entities, which are separate inputs.
"""
import os
import sys
import xml.etree.ElementTree as ElementTree

BASE = "{http://www.w3.org/XML/1998/namespace}base"


def walk(element, folder, out, root):
    base = element.get(BASE)
    folder = os.path.normpath(os.path.join(folder, base)) if base else folder

    for child in element:
        if child.tag == "TESTCASES":
            walk(child, folder, out, root)
        elif child.tag == "TEST":
            out.append((child, folder))


def main():
    root = sys.argv[1]
    tests = []

    for directory, _, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith(".xml"):
                continue

            path = os.path.join(directory, name)

            try:
                tree = ElementTree.parse(path)
            except (ElementTree.ParseError, ValueError, LookupError):
                continue

            if tree.getroot().tag in ("TESTSUITE", "TESTCASES"):
                walk(tree.getroot(), directory, tests, root)

    lines = set()

    for test, folder in tests:
        edition = test.get("EDITION")
        version = test.get("VERSION")
        kind = test.get("TYPE")

        if edition and "5" not in edition.split():
            continue

        if version and version != "1.0":
            continue

        if kind not in ("valid", "invalid", "not-wf"):
            continue

        # Namespaces in XML is a different specification (its constraints are checked by namespace-aware processors).
        if "namespaces" in test.get("URI") or "/namespaces/" in folder + "/":
            continue

        # Encodings other than UTF-8 are transcoded before parsing; their documents are not UTF-8 text.
        path = os.path.normpath(os.path.join(folder, test.get("URI")))

        if not os.path.exists(path):
            continue

        data = open(path, "rb").read()

        if data[:2] in (b"\xff\xfe", b"\xfe\xff") or data[:4] in (b"<\x00?\x00", b"\x00<\x00?"):
            continue

        declaration = data[:200].split(b"?>")[0]

        if b"encoding" in declaration and not any(known in declaration.lower() for known in (b"utf-8", b"us-ascii")):
            continue

        # Tests of external entities (their content is another file) are not documents of their own.
        if test.get("ENTITIES") in ("general", "both", "parameter") and kind == "not-wf":
            continue

        lines.add(("accept/" if kind != "not-wf" else "reject/") + os.path.relpath(path, root))

    print("\n".join(sorted(lines)))


main()
