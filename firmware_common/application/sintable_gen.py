from math import pi, sin, inf

fixp_pi = int(pi * 2**15 + 0.5)
print(f"#define FIXP_PI {fixp_pi}\n")


# Keep in sync with algorithm used in c code.
def intsin(x):
    sign = 1
    if x < 0:
        x = -x
        sign = -1

    if x > 0x4000:
        x = 0x8000 - x

    idx = x >> 8
    delta = x & 0xFF
    # The following modification gets more accuracy by using the nearest point in the lut.
    # The max abs error reduces from 11 to 3. However we don't need the accuracy, but want the speed.
    # if delta >= 0x80:
    #     idx += 1
    #     delta -= 0x100
    delta = (delta * fixp_pi) >> 15

    t0 = lut[idx]
    t1 = (delta * lut[64 - idx]) >> 15
    return sign * (t0 + t1)


# See if result would overflow s16. Only need to check the 0 - 0x4000 range
def check_overflow():
    m = max(intsin(x) for x in range(0x4001))
    return m >= 0x8000


# Start with max scale, reduce until the lut won't cause overflow.
scale = 2**15
while True:
    lut = [int(sin(pi * i / 128) * scale + 0.5) for i in range(65)]
    if not check_overflow():
        break
    scale -= 1

# Print out table for easy copy-paste to c code.
lut_contents = ",".join(str(x) for x in lut)
print(f"static const s16 as16SinLut[65] = {{{lut_contents}}};")

with open("sin_lut_dbg.csv", "w") as f:
    f.write("x,hex x,true y,calc_y,abs err,rel err\n")
    for x in range(-0x8000, 0x8000):
        true_y = int(sin(x * pi / 2**15) * 2**15)
        calc_y = intsin(x)

        abs_err = abs(true_y - calc_y)
        if true_y == 0:
            rel_err = 0.0
        else:
            rel_err = abs_err / abs(true_y)

        f.write(f"{x},{hex(x)},{true_y},{calc_y},{abs_err},{rel_err}\n")
