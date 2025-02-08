# Calling Convention

Reference: [Calling Convention](https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf)

## C Datatypes and Alignment

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

Table 18.1: C compiler datatypes for base RISC-V ISA

## Registers

 | Register | ABI Name | Description                      | Saver  |
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

Table18.2:RISC-Vcallingconventionregisterusage

### Caller-Saved Registers
| Register | ABI Name | Description                      |
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

### Callee-Saved Registers
| Register | ABI Name | Description                  |
| -------- | -------- | ---------------------------- |
| x2       | `sp`     | Stack pointer                |
| x8       | `s0/fp`  | Saved register/frame pointer |
| x9       | `s1`     | Saved register               |
| x18–27   | `s2–11`  | Saved registers              |
| f8–9     | `fs0–1`  | FP saved registers           |
| f18–27   | `fs2–11` | FP saved registers           |

### Other Registers
| Register | ABI Name | Description     |
| -------- | -------- | --------------- |
| x0       | `zero`   | Hard-wired zero |
| x3       | `gp`     | Global pointer  |
| x4       | `tp`     | Thread pointer  |
