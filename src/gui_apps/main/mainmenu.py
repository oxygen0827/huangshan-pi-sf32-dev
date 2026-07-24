#!/usr/bin/env python3

import sys


def generateR2M(rw, mShift, s, emit=True):
    r_2_m = []
    rw = float(rw)
    for r in range(0, int(1.8 * rw)):
        if r == 0:
            m = 2**12
        else:
            m = 1.11111 * (
                (r * 120 / rw)
                - 0.4342944819e-1 * s * 120 * 10.0 ** ((r * 120 / rw) / (s * 120.0))
                + 0.043429 * s * 120
            ) / (r * 120 / rw)
            m = int(m * (2**mShift))
        r_2_m.append(m)
    if emit:
        print("r_2_m[{}]:\n{}".format(len(r_2_m), r_2_m))
    return r_2_m


def generateR2D(rw, nrShift, s, emit=True):
    r_2_d = []
    rw = float(rw)
    for r in range(0, int(1.8 * rw)):
        dp = (7.0 / 22 - (7.0 / 220) * 10 ** (r / (rw * s))) / (63.0 / 220)
        dp = int(dp * (2**nrShift))
        r_2_d.append(dp)
    if emit:
        print("r_2_d[{}]:\n{}".format(len(r_2_d), r_2_d))
    return r_2_d


def run_self_test():
    expected = 18
    r_2_m = generateR2M(10, 12, 1.8, emit=False)
    r_2_d = generateR2D(10, 12, 1.8, emit=False)
    assert len(r_2_m) == expected and r_2_m[0] == 2**12
    assert len(r_2_d) == expected
    print("mainmenu generator self-test ok")


def main(argv):
    if argv == ["--self-test"]:
        run_self_test()
        return 0
    if len(argv) != 2:
        raise SystemExit("usage: mainmenu.py <rw> <shift> | --self-test")
    rw, shift = int(argv[0]), int(argv[1])
    if rw <= 0 or shift < 0:
        raise SystemExit("rw must be positive and shift must be non-negative")
    generateR2M(rw, shift, 1.8)
    generateR2D(rw, shift, 1.8)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
