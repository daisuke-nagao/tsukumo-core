# RISC-V calling convention

This note is a quick reference to the RISC-V calling convention: the size of
each C type and the role of each register. Use it when you read or write the
kernel's assembly code, such as the trap entries in `kernel/asm/rv32/start.S`.
The tables come from the
[RISC-V calling convention](https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf).

The kernel is built for RV32 (`rv32imac` with the `ilp32` ABI, set in
`toolchain.cmake`). That target has no floating-point extension, so the `f`
registers below do not exist on it.

## C data types and alignment

| C type        | Description              | Bytes in RV32 | Bytes in RV64 |
| ------------- | ------------------------ | ------------- | ------------- |
| `char`        | Character value/byte     | 1             | 1             |
| `short`       | Short integer            | 2             | 2             |
| `int`         | Integer                  | 4             | 4             |
| `long`        | Long integer             | 4             | 8             |
| `long long`   | Long long integer        | 8             | 8             |
| `void*`       | Pointer                  | 4             | 8             |
| `float`       | Single-precision float   | 4             | 4             |
| `double`      | Double-precision float   | 8             | 8             |
| `long double` | Extended-precision float | 16            | 16            |

Table 18.1 of the reference: C compiler data types for the base RISC-V ISA.

## Registers

| Register | ABI name | Description                      | Saver  |
| -------- | -------- | -------------------------------- | ------ |
| x0       | `zero`   | Hard-wired zero                  | —      |
| x1       | `ra`     | Return address                   | Caller |
| x2       | `sp`     | Stack pointer                    | Callee |
| x3       | `gp`     | Global pointer                   | —      |
| x4       | `tp`     | Thread pointer                   | —      |
| x5–7     | `t0–2`   | Temporaries                      | Caller |
| x8       | `s0/fp`  | Saved register/frame pointer     | Callee |
| x9       | `s1`     | Saved register                   | Callee |
| x10–11   | `a0–1`   | Function arguments/return values | Caller |
| x12–17   | `a2–7`   | Function arguments               | Caller |
| x18–27   | `s2–11`  | Saved registers                  | Callee |
| x28–31   | `t3–6`   | Temporaries                      | Caller |
| f0–7     | `ft0–7`  | FP temporaries                   | Caller |
| f8–9     | `fs0–1`  | FP saved registers               | Callee |
| f10–11   | `fa0–1`  | FP arguments/return values       | Caller |
| f12–17   | `fa2–7`  | FP arguments                     | Caller |
| f18–27   | `fs2–11` | FP saved registers               | Callee |
| f28–31   | `ft8–11` | FP temporaries                   | Caller |

Table 18.2 of the reference: RISC-V calling convention register usage. The
same registers are grouped by saver below.

### Caller-saved registers

A called function may change these registers. A caller that needs one of
their values after the call saves it first.

| Register | ABI name | Description                      |
| -------- | -------- | -------------------------------- |
| x1       | `ra`     | Return address                   |
| x5–7     | `t0–2`   | Temporaries                      |
| x10–11   | `a0–1`   | Function arguments/return values |
| x12–17   | `a2–7`   | Function arguments               |
| x28–31   | `t3–6`   | Temporaries                      |
| f0–7     | `ft0–7`  | FP temporaries                   |
| f10–11   | `fa0–1`  | FP arguments/return values       |
| f12–17   | `fa2–7`  | FP arguments                     |
| f28–31   | `ft8–11` | FP temporaries                   |

### Callee-saved registers

A called function that changes one of these registers restores it before it
returns.

| Register | ABI name | Description                  |
| -------- | -------- | ---------------------------- |
| x2       | `sp`     | Stack pointer                |
| x8       | `s0/fp`  | Saved register/frame pointer |
| x9       | `s1`     | Saved register               |
| x18–27   | `s2–11`  | Saved registers              |
| f8–9     | `fs0–1`  | FP saved registers           |
| f18–27   | `fs2–11` | FP saved registers           |

### Other registers

The reference gives no saver for these registers.

| Register | ABI name | Description     |
| -------- | -------- | --------------- |
| x0       | `zero`   | Hard-wired zero |
| x3       | `gp`     | Global pointer  |
| x4       | `tp`     | Thread pointer  |
