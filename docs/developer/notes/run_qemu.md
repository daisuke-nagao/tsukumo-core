# Running the kernel and its test suites on QEMU

This page is a reference for developers who run the kernel image or its test
suites on QEMU, the emulator the project uses in place of RISC-V hardware. It
lists the launch scripts that the build writes, the QEMU command lines the
tests use, and what each test suite checks. For the steps to build and run all
tests, the presets and the PC unit tests, see [Testing](../testing.md). For the
kernel rules that the suites check, see
[Kernel behavior](../../kernel/behavior.md).

## Run the kernel image

The kernel image `tsukumo-core.elf` carries no tests: its `usermain()`, the
function that the kernel's initial task calls, is empty (`kernel/usermain.c`).
Booted on QEMU, it idles until you stop QEMU with Ctrl+A, then X. The tests
are separate ELFs; see [How the test suites run](#how-the-test-suites-run).

### Launch scripts

When CMake finds QEMU at configure time, it writes four launch scripts into the
build directory (`../build/debug` for the `debug` preset). Each script starts
QEMU on the `tsukumo-core.elf` of that build directory.

| Script | Host | What it does |
| --- | --- | --- |
| `run_qemu.sh` | Linux | Boots the kernel image. |
| `run_qemu_debug.sh` | Linux | Boots the kernel image in debug mode, with instruction-level tracing (`-d in_asm`). |
| `run_qemu.bat` | Windows | Boots the kernel image. Run it from Command Prompt. |
| `run_qemu_debug.bat` | Windows | Boots the kernel image with instruction-level tracing. Run it from Command Prompt. |

On Linux, run a script from the build directory:

```sh
./run_qemu.sh
```

```sh
./run_qemu_debug.sh
```

CMake writes the `.sh` scripts without the execute permission (the
`file(CHMOD ...)` lines in `CMakeLists.txt` are commented out). Before the
first run, make them executable with `chmod +x run_qemu.sh run_qemu_debug.sh`,
or start them with `sh run_qemu.sh`.

### QEMU options in the scripts

| Option | Effect |
| --- | --- |
| `-machine virt` | Emulates QEMU's `virt` board. |
| `-kernel tsukumo-core.elf` | Loads the kernel image. |
| `-nographic` | Opens no window and connects the serial console to the terminal. |
| `-bios none` | Loads no firmware, so QEMU starts the kernel image directly. |
| `-d in_asm` | Debug scripts only. Writes an instruction-level assembly log of the guest code, for debugging. |

## How the test suites run

Each test suite is its own ELF, `tsukumo-core-test-<suite>.elf`. When CMake
finds QEMU at configure time, it registers each suite with CTest, CMake's test
driver, as the test `qemu-<suite>`. `ctest` does not rebuild, so build first,
then run:

```sh
ctest --test-dir <build dir> --output-on-failure --no-tests=error
```

`--no-tests=error` makes `ctest` fail when no test was registered (QEMU was not
found at configure time) instead of reporting success.
[Testing](../testing.md#run-the-qemu-tests) gives the full procedure.

### The test runner

Most suites use the shared test runner in `test/runner.c`, which is built on
Unity, the C unit-test framework that the build downloads at configure time.
The runner runs each case in its own task, prints a result line for each case
and a summary, and ends QEMU with exit status 0 if no case failed and 1 if one
did. A case that does not finish in time is reported as a timeout and aborts
its suite. Every QEMU test also has a CTest time limit of 30 seconds.

A plain suite passes when QEMU exits with status 0 and the output has no
`:FAIL:` line. Some suites instead print a marker line that the test requires,
and the `selftest-*` suites are built to fail. The suite tables below name each
marker; [How CTest decides a result](../testing.md#how-ctest-decides-a-result)
gives the full rules.

### Guest clock and host-clock runs

By default each test runs QEMU on an instruction-count clock:

```sh
qemu-system-riscv32 -machine virt -bios none -nographic -icount shift=5,sleep=off -kernel <suite ELF>
```

With `-icount`, guest time follows the number of instructions the guest has
run instead of the host clock: one instruction takes 2^shift ns, so 32 ns at
shift 5, and a 10 ms kernel tick holds 312500 instructions. With `sleep=off`,
guest time jumps to the next timer deadline while the idle task halts in `wfi`
(the RISC-V wait-for-interrupt instruction), instead of waiting in real time.
The tests wait on timers, so on the host clock a stalled host stretches the
guest's waits and runs differ. On the instruction-count clock the UART log is
the same on every run, and a suite takes a fraction of a second.

A fixed schedule can hide a race that only the host clock shows. So every suite
that is meant to pass also runs once on the host clock, as
`qemu-<suite>-hostclock`, with this command:

```sh
qemu-system-riscv32 -machine virt -bios none -nographic -kernel <suite ELF>
```

These tests carry the label `host-clock`: `ctest -L host-clock` runs only them,
and `ctest -LE host-clock` skips them. They run one at a time (CTest
`RUN_SERIAL`), because a stalled host would stretch the guest's waits.

The cache variable `QEMU_ICOUNT_SHIFT` selects the clock:

| `QEMU_ICOUNT_SHIFT` | Main tests run with | Main tests one at a time | `-hostclock` tests | Instruction-count suites |
| --- | --- | --- | --- | --- |
| A number (default `5`) | `-icount shift=<number>,sleep=off` | No | Registered | Registered only for a number of 8 or less |
| `auto` | `-icount shift=auto`, which lets QEMU follow the host clock | Yes | Registered | Not registered |
| Empty (`-DQEMU_ICOUNT_SHIFT=`) | The host clock | Yes | Not registered | Not registered |

The instruction-count suites are `idle-halt`, `mailbox-cost`, `timer-cost` and
`timer-cost-max`. They read `minstret`, the RISC-V counter of retired
instructions. QEMU counts instructions in `minstret` only under `-icount`; on
the host clock the counter returns host timer ticks, which are not comparable.
These suites therefore have no `-hostclock` run. They also depend on how many
instructions fit in a unit of guest time: with `auto` that ratio follows the
host, and with a shift above 8 a spinning idle task would run too few
instructions for `idle-halt` to tell it from a halted one. When CMake skips one
of these suites, it warns at configure time: `<suite>: not run, it needs
QEMU_ICOUNT_SHIFT to be a number of 8 or less`.

## Terms used in the suite descriptions

| Term | Meaning |
| --- | --- |
| `minstret` | The RISC-V counter of retired instructions. |
| `mcause`, `mepc`, `mtval` | RISC-V machine trap registers: the cause of a trap, the address of the instruction that trapped, and the value that caused it (such as the bad address or instruction word). |
| `MIE`, `mie` | `MIE` is the global interrupt-enable bit of `mstatus`. `mie` is a separate register with one enable bit for each interrupt source. See [Kernel behavior](../../kernel/behavior.md#terms-used-on-this-page). |
| ISR | Interrupt service routine. The kernel's trap entries count their nesting in `tkmc_isr_depth` (the ISR depth), and code runs as an ISR while that count is not 0. See [Kernel behavior](../../kernel/behavior.md#terms-used-on-this-page). |
| PLIC | The platform-level interrupt controller of the `virt` board. It routes external interrupts, here from the UART and the RTC (real-time clock), to the CPU. |
| TCB | Task control block, the kernel's record of a task. |
| Tick, tick edge | The kernel's timer period of 10 ms, and the moment one tick ends. |
| Link-time wrap | The linker option `--wrap=<function>`. Calls to `<function>` go to the suite's `__wrap_<function>`, which can call the original as `__real_<function>`. Suites use it to observe a kernel function on the real trap path. |
| Start code | The `stacd` argument of `tk_sta_tsk()`, which the task's entry function receives. |
| `tkmc_` prefix | Marks a name of this kernel's own interface, not part of the μT-Kernel API. |

## Test suites

Unless its section or the Notes column says otherwise, each suite below is one
test, `qemu-<suite>`, and also has a `qemu-<suite>-hostclock` run.

### Kernel API suites

| Suite | What it checks | Notes |
| --- | --- | --- |
| `task-timer` | Sleeping, waking, releasing a wait and delaying: `tk_slp_tsk()`, `tk_wup_tsk()`, `tk_rel_wai()`, `tk_dly_tsk()`. | |
| `task-restart` | Restarts one task 100 times. Its runs end both by `tk_ext_tsk()` and by returning. | |
| `task-create` | What `tk_cre_tsk()` refuses with `E_PAR`: the priority, the stack size, a NULL entry or buffer, the alignment of the stack end, and a stack end that wraps around the 32-bit address space. One case per input. It starts no task. | The PC unit tests check the same rules, but only this suite checks the 32-bit wrap; on the PC the stack end wraps at 64 bits. |
| `event-flag` | Event flags: `tk_cre_flg()`, `tk_wai_flg()`, `tk_set_flg()`. | |
| `semaphore` | Semaphores: `tk_cre_sem()`, `tk_wai_sem()`, `tk_sig_sem()`, `tk_del_sem()`, including the order in which a new request may take units ahead of the tasks that wait. | See [semaphore-order-mutation.md](semaphore-order-mutation.md). |
| `semaphore-wait-release` | Where a signalled unit goes when the head of the wait queue leaves unserved, by a time-out, by `tk_rel_wai()` or by the deletion of the semaphore. Each under `TA_TFIFO` and `TA_TPRI`, with `TA_FIRST` and with `TA_CNT`. | |
| `mailbox` | Mailboxes: `tk_cre_mbx()`, `tk_snd_mbx()`, `tk_rcv_mbx()`, `tk_del_mbx()`. | |
| `memset` | `tkmc_memset()`, from a copy of `kernel/memset.c` built with the undefined-shift check in trap mode. The first undefined shift stops the kernel at an illegal-instruction trap, and `test/panic_hook.c` prints the cause. | The same matrix runs on the PC with UBSan (`test/host/test_memset.c`). |
| `memcpy` | `tkmc_memcpy()` and `tkmc_memset()` of the kernel library as callers get them, on bytes and on objects of other types. | |
| `wait-context` | The wait calls `tk_dly_tsk()`, `tk_slp_tsk()`, `tk_wai_sem()`, `tk_wai_flg()` and `tk_rcv_mbx()` return `E_CTX` and change nothing when a task calls them with interrupts disabled by `DI()`. It also checks that `DI(status)` fills a caller's variable named `status`: the macro used to declare a local of that name, which hid the caller's variable. | Real traps are covered by the `task-context-*` suites. |
| `interrupt-state` | `EI()` puts back the interrupt state that `DI()` saved, whatever the state is when `EI()` runs, and `EI(0)` turns interrupts on. | |
| `wakeup-context` | `tk_wup_tsk()` called from the real timer trap: the ISR that interrupted a task may queue a wake-up request for that task, while the task itself may not. | Uses a link-time wrap of `tkmc_timer_handler()`. See [wakeup-context-mutation.md](wakeup-context-mutation.md). |
| `interrupt` | External interrupts through the PLIC, the interrupt stack and nested handlers. | See [The interrupt suite](#the-interrupt-suite). |

### Timer suites

| Suite | What it checks | Notes |
| --- | --- | --- |
| `timer-wrap` | Moves the machine timer to 100 ms before the wrap of its low 32-bit word, with a nonzero high word, before it starts the runner. A 300 ms delay must finish after the wrap. | A separate ELF keeps this clock jump out of the other suites. |
| `timer-switch` | Which trap switches tasks, through a link-time wrap of `schedule()` that records the `mcause` of the trap it runs in. A task that a timer interrupt wakes must be switched to inside that timer trap; this is checked over four sleeps, because the runner can wake in the same trap and run first. A task woken by `tk_wup_tsk()` is switched to in the software interrupt. A task that a timer trap preempts keeps the registers `t0` to `t6`, `a2` to `a7` and `s1` to `s11` that it held. | |

### Instruction-count suites

These suites read `minstret`, so none has a `-hostclock` run, and CMake
registers them only when `QEMU_ICOUNT_SHIFT` is a number of 8 or less (see
[Guest clock and host-clock runs](#guest-clock-and-host-clock-runs)).

| Suite | What it checks | Notes |
| --- | --- | --- |
| `idle-halt` | The idle task halts the CPU while every task waits, and a ready task of the lowest priority still runs without a tick. | See [The idle-halt suite](#the-idle-halt-suite). |
| `mailbox-cost` | How long mailbox calls keep interrupts disabled. | See [The mailbox-cost suite](#the-mailbox-cost-suite). |
| `timer-cost` | How long the timer interrupt handler runs when many timed waits end in one tick. | See [The timer-cost suite](#the-timer-cost-suite). |
| `timer-cost-max` | The worst cases of `timer-cost` again, at the kernel's limit of 31 waits. | No runner. Requires `TIMER COST MAX OK`. See [The timer-cost-max suite](#the-timer-cost-max-suite). |

### Startup and fatal-stop suites

| Suite | What it checks | Notes |
| --- | --- | --- |
| `bss-clear` | The kernel clears `.bss` at boot. QEMU starts with zeroed RAM, so `test/bss_dirty_entry.S`, linked first, fills `.bss` with 0xa5 and then starts the kernel, as a board with dirty RAM would. The entry also leaves a value in `a0`, as boot code does, and `usermain()` must receive it. | No runner. Requires `BOOT ARG OK`, then `BSS OK`, which it prints if no 0xa5 is left. |
| `startup-<case>` | Six ELFs: `success`, `initial-create`, `initial-start`, `idle-create`, `idle-start` and `idle-start-mie`. At startup the kernel creates and starts the initial task and the idle task. A link-time wrap makes one of those four calls fail, and startup must stop through `tkmc_startup_panic()` with the stage and error recorded, without reaching later steps. `success` checks normal startup through the same wraps. `idle-start-mie` injects the idle-start failure with interrupts enabled. | No runner. Requires `STARTUP SUCCESS OK` or `STARTUP FAILURE OK stage=`. |
| `panic-illegal`, `panic-load-fault`, `panic-bad-stack` | A fatal trap stops the kernel and reports its cause: an illegal instruction, a load from an unmapped address, and an illegal instruction run with a stack pointer that points at nothing. The machine stops at the trap, so each ELF causes one trap. Its replaced `tkmc_panic()` (`test/panic_hook.c`) prints `PANIC OK` only if `mcause`, `mepc` and `mtval` are as expected. | Requires `PANIC OK`. |
| `task-context-<case>` | Eight ELFs that enter the real timer and dispatch traps, or run before the first task. `isr`: calls that an ISR may make, from the real timer trap, and the task switch that follows. `no-current-wait`: wait calls made before any task runs. `di-ext`, `di-exd`, `isr-ext`, `isr-exd`, `no-current-ext` and `no-current-exd`: `tk_ext_tsk()` or `tk_exd_tsk()` called with interrupts disabled, from an ISR, or with no current task. The kernel must stop through `tkmc_context_panic()` with `E_CTX` recorded and the task state unchanged. | All but `isr` have no runner. Requires `1 Tests 0 Failures` (`isr`), `NO CURRENT WAIT OK` (`no-current-wait`) or `CONTEXT STOP OK` (the others). |

### Test-runner self-tests

These suites check the test runner in `test/runner.c`. All but `selftest-pass`
are built to fail and form a pair of tests: `qemu-selftest-<name>-expectfail`
passes when the run fails, and `qemu-selftest-<name>-output` checks which cases
failed and how many ran. Run both tests of a pair, because `-expectfail` also
passes when QEMU fails to start or the ELF is missing. The pairs have no
`-hostclock` run.

| Suite | The runner must |
| --- | --- |
| `selftest-fail` | End only the case whose assert failed. |
| `selftest-child-fail` | Fail the case whose child task failed an assert, end only the child, and leave the later cases alone. |
| `selftest-hang` | Report a case that never ends as a timeout and abort the suite. |
| `selftest-hang-child` | Time out a case whose child spins forever after the case has returned, and keep polling while the child spins. |
| `selftest-orphan` | Fail the case whose child fails after the case has ended, not the next case. |
| `selftest-nocheck` | Fail `test_join_expect()` when a child ends without reaching its checkpoint. |
| `selftest-pass` | Exit with status 0 when every case passes, and wait in `test_join()` for the children. This is one plain test, `qemu-selftest-pass`, that must pass. |

### Build check

| Test | What it checks | Notes |
| --- | --- | --- |
| `word-access-tbaa` | Compiles `kernel/memcpy.c` and `kernel/memset.c` to LLVM IR and checks that their word loops access memory as a type that may alias any object (`test/check_word_tbaa.cmake`). | Runs no QEMU. CMake registers it only together with the QEMU suites, so a build without QEMU still has no tests and `--no-tests=error` still fails. |

## Suite details

### The interrupt suite

`qemu-interrupt` tests `tk_def_int()`, `EnableInt()` and `DisableInt()` with
the transmitter-empty interrupt of the UART (PLIC source 10), which software
raises and clears. It checks that:

- The handler runs as an ISR with interrupts disabled and receives its
  interrupt number.
- A task that the handler wakes is switched to in the same trap. A link-time
  wrap of `schedule()` records `mcause` to show this.
- A disabled source waits until it is enabled, and the default handler
  switches off a source that has no handler.
- Bad interrupt numbers, levels and definitions are refused.
- The handler, the timer handler and `schedule()` run on the interrupt stack
  with an ISR depth of 1 in all three traps (timer, external interrupt and
  software interrupt). The suite also measures how much of that stack the
  deepest paths use; see
  [Interrupt stack](../../kernel/interrupts.md#interrupt-stack).
- A task keeps every register, `s0` included, through a trap.

For nesting, the suite raises the goldfish RTC of the `virt` board (PLIC
source 11) the same way, by setting an alarm at a time that has already
passed. An alarm in the future would follow the host's clock, not the guest's.
The RTC interrupt nests in a UART handler that turns interrupts on, and the
suite checks that:

- It is served inside the handler when its priority is higher, and after the
  handler when its priority is the same or lower.
- The timer interrupt waits for the handler.
- A task that the nested handler wakes runs once, after the outermost handler.
- A task keeps every register through a nested trap.

Finally, a task ends while the UART interrupt is pending. `mie` masks the
interrupt until that moment, so the window always occurs. The handler then
either starts the task again or creates a new task in the TCB that
`tk_exd_tsk()` freed. The new run must begin at the task's entry with the new
start code, and the run that ended must not go on.

What QEMU cannot show is tested by the PC unit tests. See
[Nested interrupts](../../kernel/interrupts.md#nested-interrupts) for the rules.

### The idle-halt suite

`qemu-idle-halt` has two cases:

- The halt case reads `minstret` around a 100 ms sleep. It checks that the
  sleep took about that long and that the guest retired between 1000 and
  100000 instructions, so an idle task that spins fails it.
- The yield case runs a child of the lowest priority through repeated
  `tkmc_yield()` calls with only software interrupts enabled. The child calls
  the kernel's own yield because there is no public one: `tk_rot_rdq()` is not
  provided, and `tk_dly_tsk(0)` does not yield. Each yield must resume without
  a timer tick, so an idle task that halts despite this ready child times out.

### The mailbox-cost suite

`qemu-mailbox-cost` reads `minstret` around one mailbox call, which gives how
long the call keeps interrupts disabled.

- Three calls must cost the same with any number of queued messages:
  `tk_del_mbx()`, a `TA_MPRI` send at or behind the last queued message, and a
  send ahead of every message. Each is measured on a mailbox that holds 256
  messages and on an empty one, and the counts may differ by at most 100
  instructions. The two sends are also measured with 1 queued message.
- A send between two queued messages scans the queue, so its time grows with
  the queue. With 256 queued messages it is held to a budget of 31250
  instructions (a tenth of a tick at shift 5). Two cases check it: a send into
  the middle, and a send before the last message, which is the worst case.
- After each send, the case receives every message and checks that the sent
  one came out where the case meant it to.

See [mailbox-di-budget.md](mailbox-di-budget.md) for the budget and the
measurements.

### The timer-cost suite

`qemu-timer-cost` wraps `tkmc_timer_handler()` at link time and reads
`minstret` around it. Its cases:

- 1, 8 and 29 timed waits that end in the same tick.
- 29 waits with limits one tick apart: on time, 1 tick late and 29 ticks late.
  Interrupts are held off, so that one handler call ends every wait it reaches.
- 29 `TA_CNT` semaphore waits that time out 29 ticks late.
- No waits, with an interrupt 1 tick and 200 ticks late.

Twenty-nine is the most the suite can reach: the kernel has 32 tasks, and the
idle task, the runner and the case task take three of them.

Each case reports the instructions of the most expensive handler call. It fails
when that count is over the budget of 31250 instructions (a tenth of a tick at
shift 5), when the interrupt was not as late as planned, when the waits did not
end as planned or not in the order of their deadlines, or when the semaphore
waits cost much more than the same delays (the semaphore margin). See
[timer-delay-budget.md](timer-delay-budget.md) and
[timer-handler-cost.md](timer-handler-cost.md).

### The timer-cost-max suite

`qemu-timer-cost-max` measures the two late cases with limits one tick apart,
`tk_dly_tsk()` waits and time-outs on a `TA_CNT` semaphore, at the kernel's
limit of 31 waits (every task but the idle task). `qemu-timer-cost` cannot
reach that limit because its runner and case task take two tasks.

- It has no runner. `usermain()` creates 30 tasks and waits itself.
- The link-time wrap of the handler makes the call 31 ticks late, inside the
  trap, by spinning with interrupts disabled.
- It checks the budget, the semaphore margin, that the timer queue lists the
  waits longest first and that they began in one tick, and that the call found
  32 tick edges and ended 31 waits.
- It prints its counts on lines that start with `timer-cost-max:` and passes
  only when it prints `TIMER COST MAX OK`.
