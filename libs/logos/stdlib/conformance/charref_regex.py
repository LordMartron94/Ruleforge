#!/usr/bin/env python3
"""Writes the Genesis expression for the character references of XML that name a legal Char (WFC: Legal Character).

  charref_regex.py   prints the body of the $_char_reference template of ../XMLCharacters.gen

Char ::= #x9 | #xA | #xD | [#x20-#xD7FF] | [#xE000-#xFFFD] | [#x10000-#x10FFFF]. A reference is '&#' or '&#x', any number of
leading zeros, and the digits of a legal value; the digit patterns below are generated so that no value is out of range.
"""
LEGAL = [(0x9, 0x9), (0xA, 0xA), (0xD, 0xD), (0x20, 0xD7FF), (0xE000, 0xFFFD), (0x10000, 0x10FFFF)]


def digit_class(low, high, base):
    names = "0123456789abcdef"
    parts = []

    def one(lo, hi, upper):
        sets = []
        if lo == hi:
            sets.append("'%s'" % (names[lo].upper() if upper else names[lo]))
        else:
            sets.append("'%s'..'%s'" % (names[lo].upper() if upper else names[lo], names[hi].upper() if upper else names[hi]))
        return sets

    decimal = min(high, 9)
    if low <= decimal:
        parts += one(low, decimal, False)

    if base == 16 and high >= 10:
        lo = max(low, 10)
        parts += one(lo, high, False) + one(lo, high, True)

    return "(" + " || ".join(parts) + ")" if len(parts) > 1 else parts[0]


def patterns(low, high, length, base):
    """Alternatives matching the numbers low..high that all have `length` digits."""
    if length == 1:
        return [[(low, high)]]

    step = base ** (length - 1)
    result = []
    first_low, first_high = low // step, high // step

    for first in range(first_low, first_high + 1):
        sub_low = low - first * step if first == first_low else 0
        sub_high = high - first * step if first == first_high else step - 1
        for tail in patterns(sub_low, sub_high, length - 1, base):
            result.append([(first, first)] + tail)

    # Merge neighbours that share the same tail into one class.
    merged = []
    for pattern in result:
        if merged and merged[-1][1:] == pattern[1:] and merged[-1][0][1] + 1 == pattern[0][0]:
            merged[-1][0] = (merged[-1][0][0], pattern[0][1])
        else:
            merged.append([list(pattern[0]) and tuple(pattern[0])] + pattern[1:])
    return merged


def compress(classes):
    """Writes runs of the same digit class as a count: '0'..'9' '0'..'9' '0'..'9' is '0'..'9'{3}."""
    parts = []
    index = 0
    while index < len(classes):
        run = 1
        while index + run < len(classes) and classes[index + run] == classes[index]:
            run += 1
        parts.append(classes[index] if run == 1 else "%s{%d}" % (classes[index], run))
        index += run
    return " ".join(parts)


def expression(base):
    alternatives = []

    for low, high in LEGAL:
        for length in range(len(format(low, "x" if base == 16 else "d")), len(format(high, "x" if base == 16 else "d")) + 1):
            length_low = max(low, base ** (length - 1) if length > 1 else 0)
            length_high = min(high, base ** length - 1)
            if length_low > length_high:
                continue
            for pattern in patterns(length_low, length_high, length, base):
                alternatives.append(compress([digit_class(a, b, base) for a, b in pattern]))

    return alternatives


if __name__ == "__main__":
    decimal = expression(10)
    hexadecimal = expression(16)
    print("    $_char_reference = ('&#' '0'* (")
    print("                          " + " ||\n                          ".join(decimal))
    print("                        ) ';') ||")
    print("                       ('&#x' '0'* (")
    print("                          " + " ||\n                          ".join(hexadecimal))
    print("                        ) ';')")
