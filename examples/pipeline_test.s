.data
.dword 10
.dword 25

.text

lui x3, 0x10000
add x4, x5, x6
add x7, x8, x9

ld x10, 0(x3)          # Load the first double from memory into x10
ld x11, 8(x3)          # Load the second double from memory into x11