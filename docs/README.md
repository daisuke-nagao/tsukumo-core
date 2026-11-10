# Documentation index

This index lists the documents of tsukumo-core by what you want to do, with the
question each one answers. Start with the [README](../README.md) to build the
kernel and run it on QEMU.

## Use the kernel

| Document | Question it answers |
| --- | --- |
| [API support status](kernel/API_Support_Status.md) | Which μT-Kernel 3.0 calls does the kernel provide? |
| [Kernel behavior](kernel/behavior.md) | What happens when startup fails, which calls work in which context, and how long do mailbox calls keep interrupts disabled? |
| [Interrupts](kernel/interrupts.md) | How do I use `DI()` and `EI()`, define an interrupt handler, and size stacks for traps and nested interrupts? |

## Understand behavior and specification conformance

| Document | Question it answers |
| --- | --- |
| [Kernel behavior](kernel/behavior.md) | Which rules does this kernel set where the specification leaves the choice to the implementation? |
| [Specification review](kernel/spec-conformance-review.md) | How does each implemented call compare with the μT-Kernel 3.0 specification, and which differences and evidence gaps remain? |
| [Specification review history](kernel/spec-conformance-history.md) | Which differences were found and fixed, and when were the checks run? |

## Build, test and change the kernel

| Document | Question it answers |
| --- | --- |
| [Contributing](../CONTRIBUTING.md) | How do I set up the repository, check a change and submit a pull request? |
| [Building and running the tests](developer/testing.md) | How do I run the QEMU and PC tests, at every optimization level, and how is each result judged? |
| [Running on QEMU](developer/notes/run_qemu.md) | How do I start QEMU with the generated scripts, and what does each QEMU test suite check? |
| [RISC-V calling convention](developer/notes/calling-convention.md) | Which registers must a function save, and how large is each C type on RV32? |

## Measurement and test records

These notes record a measurement or an experiment on a given date: the method,
the results and the limits.

| Document | Question it answers |
| --- | --- |
| [Timer handler cost](developer/notes/timer-handler-cost.md) | How many instructions does the timer handler take when many waits end in one tick? |
| [Timer handler budget](developer/notes/timer-delay-budget.md) | What is the timer handler's instruction budget, and does its worst case stay inside it? |
| [Mailbox interrupt-disabled time](developer/notes/mailbox-di-budget.md) | How long does a mailbox send into a long queue keep interrupts disabled, and for how many messages does the budget hold? |
| [Optimization and LTO validation](developer/notes/optimization-lto-validation.md) | Do the memory copy routines and mailbox links stay correct at every optimization level, with and without link-time optimization? |
| [Semaphore order checks](developer/notes/semaphore-order-mutation.md) | Which wrong versions of the semaphore request-order rule do the tests catch? |
| [Wake-up context checks](developer/notes/wakeup-context-mutation.md) | Which faults in the `tk_wup_tsk` context check do the tests detect? |
