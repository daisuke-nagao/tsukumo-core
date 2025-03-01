# Interrupt Status (`intsts`) Declaration in `DI/EI` Usage

## Overview

In uT-Kernel, the `DI()` and `EI()` macros are used to disable and enable interrupts, respectively. However, these macros require an `intsts` variable to store the previous interrupt status. The specification does not explicitly state whether the caller must declare `intsts` or if it is automatically managed by `DI()`.

From the `isDI()` usage example provided in the specification, it becomes evident that `intsts` must be declared by the caller before invoking `DI()` and `EI()`. This document clarifies this requirement to avoid ambiguity and ensure correct usage.

## Requirement for `intsts` Declaration

The caller is responsible for declaring `intsts` before calling `DI()` or `EI()`. Failing to do so may result in undefined behavior or compilation errors.

### Correct Usage Example

```c
void foo() {
    UINT intsts; // User must declare intsts
    DI(intsts);
    if (isDI(intsts)) {
        // Interrupts were already disabled
    } else {
        // Interrupts were enabled before DI()
    }
    EI(intsts);
}
```

### Incorrect Usage Example

```c
void foo() {
    DI(intsts); // Error: intsts is not declared
    EI(intsts);
}
```

## References

- uT-Kernel 3.0 Specification
- Example usage in existing uT-Kernel documentation

By explicitly stating this requirement in the documentation, developers can avoid confusion and ensure proper interrupt management in their applications.

