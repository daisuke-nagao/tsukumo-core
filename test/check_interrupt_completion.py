# SPDX-FileCopyrightText: 2026 Daisuke Nagao
#
# SPDX-License-Identifier: MIT

"""Run an interrupt suite and check the PLIC completion MMIO trace."""

import re
import subprocess
import sys
import tempfile
from pathlib import Path


EXPECTED = {
    "handler_runs_with_its_interrupt_number": [10, 10],
    "end_of_int_hlng_is_idempotent_with_a_disabled_source": [10],
    "end_of_int_allows_a_still_asserted_source_to_be_raised_again": [10, 10],
    "handler_runs_as_an_isr_and_its_wakeup_switches_at_the_trap_exit": [10],
    "a_disabled_source_waits_until_it_is_enabled": [10],
    "check_int_observes_disabled_pending_source_without_consuming_it": [10],
    "clear_int_discards_disabled_pending_uart": [10],
    "clear_int_discards_enabled_pending_uart": [10],
    "clear_int_claims_a_pending_source_below_the_threshold": [10],
    "clear_int_discards_pending_uart_with_zero_priority": [10],
    "clear_int_preserves_a_simultaneous_non_target_request": [10, 11],
    "clear_int_with_no_pending_source_has_no_completion": [],
    "clear_int_does_not_complete_an_active_uncompleted_claim": [10],
    "clear_int_handles_a_new_pending_request_after_end_of_int": [10, 10],
    "a_new_definition_replaces_the_old_one": [10],
    "without_a_handler_the_source_is_switched_off_and_served_once_defined": [10, 10],
    "bad_definitions_are_refused": [10],
    "a_handler_cannot_be_defined_from_an_isr_or_with_interrupts_off": [10],
    "a_source_a_handler_switched_off_is_served_again_when_enabled": [10, 10],
    "a_bad_number_or_level_changes_nothing": [],
    "schedule_runs_on_the_interrupt_stack_in_every_trap": [10],
    "the_timer_handler_runs_on_the_interrupt_stack": [],
    "a_task_keeps_its_registers_through_an_interrupt": [10],
    "a_task_keeps_its_registers_through_a_switch_at_the_trap_exit": [10],
    "a_trap_adds_one_to_the_depth_and_takes_it_off": [10],
    "the_interrupt_stack_has_room_for_the_deepest_paths": [10],
    "the_timer_waits_for_a_handler_that_has_interrupts_on": [11, 10],
    "a_source_of_a_higher_priority_interrupts_a_handler": [11, 10],
    "clear_int_from_inner_handler_preserves_the_active_outer_claim": [11, 10],
    "a_source_of_the_same_or_a_lower_priority_waits_for_the_handler": [10, 11, 10, 11],
    "a_task_a_nested_handler_wakes_runs_after_the_outermost_handler": [11, 10],
    "a_task_keeps_its_registers_through_a_nested_interrupt": [11, 10],
    "the_interrupt_stack_holds_nested_handlers": [11, 10],
    "asm_task_entry_and_termination_paths": [],
    "asm_task_restart_rebuilds_entry_and_arguments": [],
    "asm_task_tcb_reuse_starts_with_fresh_arguments": [],
    "asm_tk_ret_int_restores_frame_and_dispatches_in_external_trap": [10, 10],
    "asm_direct_return_restores_frame_and_leaves_dispatch_pending": [10, 10],
    "asm_outer_and_hlng_inner_dispatch_at_the_outer_exit": [11, 10],
    "hlng_outer_and_asm_inner_direct_return_defers_dispatch": [11, 10],
    "hlng_outer_and_asm_inner_tk_ret_int_defers_dispatch": [11, 10],
    "asm_outer_hlng_inner_end_outer_before_inner_exit": [10, 11],
    "hlng_outer_asm_inner_end_outer_before_inner_exit": [10, 11],
    "hlng_outer_asm_inner_tk_ret_int_end_outer_before_inner_exit": [10, 11],
    "end_of_int_from_tk_ret_int_is_idempotent": [10, 10],
    "end_of_int_from_direct_return_is_idempotent": [10, 10],
    "end_of_int_from_tk_ret_int_single_call": [10, 10],
    "end_of_int_from_direct_return_single_call": [10, 10],
    "a_task_restarted_by_an_isr_right_after_tk_ext_tsk_starts_anew": [10],
    "a_task_created_by_an_isr_in_the_tcb_tk_exd_tsk_freed_starts_anew": [10],
}


def _source(level):
    """The source of a level of the nested chain of interrupt_depth.c."""
    return 10 if level % 2 else 11


def _depth_expected(depth):
    """The EOI writes of each case of interrupt_depth.c built for depth levels.

    Level k (2 to depth-1) completes the claim of level k-1 before it raises
    level k+1, so the outer claims are complete before the chain unwinds; the
    deepest level's exit then completes its own claim and level depth-1's exit
    its own. In the late-request cases the deepest level completes level
    depth-1's claim itself, and the late request adds one more completion.
    """
    outer = [_source(level) for level in range(1, depth - 1)]
    chain = outer + [_source(depth), _source(depth - 1)]
    late = outer + [_source(depth - 1), _source(depth), _source(depth - 1)]
    cases = dict.fromkeys(DEPTH_CHAIN_CASES, chain)
    cases.update(dict.fromkeys(DEPTH_LATE_CASES, late))
    return cases


DEPTH_CHAIN_CASES = (
    "c_handlers_nest_to_the_full_depth",
    "asm_handlers_with_tk_ret_int_outside",
    "asm_handlers_with_direct_return_outside",
    "c_odd_and_asm_even_levels_with_tk_ret_int",
    "c_odd_and_asm_even_levels_with_direct_return",
    "asm_odd_and_c_even_levels_with_tk_ret_int",
    "asm_odd_and_c_even_levels_with_direct_return",
)
DEPTH_LATE_CASES = (
    "a_same_priority_request_waits_for_the_deepest_level",
    "a_lower_priority_request_waits_for_the_whole_chain",
)

# The expected sets by suite; --expect=<name> picks one, "interrupt" if absent.
EXPECTED_SETS = {"interrupt": EXPECTED}
EXPECTED_SETS.update(
    {f"interrupt-depth-{depth}": _depth_expected(depth) for depth in range(3, 8)}
)


EVENT = re.compile(r"addr (0x[0-9a-fA-F]+) value (0x[0-9a-fA-F]+) size (\d+)")


def parse_trace(lines, expected):
    seen = {}
    results = set()
    active_case = None
    uart_line = ""
    for line in lines:
        match = EVENT.search(line)
        if not match:
            continue
        address, value, size = int(match[1], 16), int(match[2], 16), int(match[3])
        if address == 0x10000000 and size == 4:
            uart_line += chr(value & 0xff)
            if "\n" in uart_line:
                *complete_lines, uart_line = uart_line.split("\n")
                for text in complete_lines:
                    if text.startswith("RUN "):
                        if active_case is not None:
                            raise RuntimeError(f"case {active_case} has no result")
                        active_case = text[4:]
                        if active_case not in expected or active_case in seen:
                            raise RuntimeError(f"unexpected or repeated case marker: {active_case}")
                        seen[active_case] = []
                    elif text.endswith(":PASS") or ":FAIL:" in text:
                        if active_case is None:
                            raise RuntimeError(f"result without an active case: {text}")
                        if f":{active_case}:FAIL:" in text:
                            raise RuntimeError(f"test case failed: {text}")
                        if not text.endswith(f":{active_case}:PASS"):
                            raise RuntimeError(f"result for another case: {text}")
                        results.add(active_case)
                        active_case = None
        elif address == 0x0C200004:
            if active_case is None:
                raise RuntimeError(f"unscoped PLIC EOI write: {value}")
            seen[active_case].append(value)

    if active_case is not None:
        raise RuntimeError(f"case {active_case} has no result")
    missing = expected.keys() - seen.keys()
    if missing:
        raise RuntimeError(f"trace did not reach cases: {sorted(missing)}")
    if results != expected.keys():
        raise RuntimeError(f"missing case results: {sorted(expected.keys() - results)}")
    for name, values in expected.items():
        if seen[name] != values:
            raise RuntimeError(f"{name}: EOI writes {seen[name]}, expected {values}")
    return seen


def self_test():
    expected = {"case-a": [10], "case-b": []}

    def uart(text):
        return [f"memory_region_ops_write addr 0x10000000 value 0x{ord(char):x} size 4" for char in text]

    def eoi(source=10):
        return f"memory_region_ops_write addr 0xc200004 value 0x{source:x} size 4"

    def trace_for(result="PASS", writes=(10,), include_b=True):
        trace = uart("RUN case-a\n") + [eoi(source) for source in writes]
        trace += uart(f"suite:1:case-a:{result}\n")
        if include_b:
            trace += uart("RUN case-b\n") + uart("suite:2:case-b:PASS\n")
        return trace

    valid = trace_for()
    assert parse_trace(valid, expected) == expected

    bad_traces = (
        [eoi()] + valid,  # orphan completion before RUN
        valid + [eoi()],  # completion after the last PASS
        uart("suite:1:case-a:PASS\n") + valid,  # result before RUN
        uart("RUN unknown\n"),  # unknown case
        uart("RUN case-a\nRUN case-a\n"),  # repeated case
        uart("RUN case-a\nsuite:1:case-b:PASS\n"),  # result for another case
        trace_for(writes=(10, 10)),  # extra completion
        trace_for(writes=()),  # missing completion
        trace_for(include_b=False),  # missing case
        trace_for(result="FAIL: broken"),  # failed result
        uart("RUN case-a\n") + [eoi()],  # missing result
    )
    for trace in bad_traces:
        try:
            parse_trace(trace, expected)
        except RuntimeError:
            continue
        raise AssertionError("malformed trace was accepted")

    assert split_expect(["--expect=interrupt-depth-3", "qemu", "-kernel", "x.elf"]) == (
        EXPECTED_SETS["interrupt-depth-3"],
        ["qemu", "-kernel", "x.elf"],
    )
    assert split_expect(["qemu", "-kernel", "x.elf"]) == (EXPECTED, ["qemu", "-kernel", "x.elf"])
    for argv in (["--expect=unknown", "qemu", "-kernel", "x.elf"], ["--expect=", "qemu"]):
        try:
            split_expect(argv)
        except RuntimeError:
            continue
        raise AssertionError(f"bad expected set was accepted: {argv[0]}")
    expected_depths = {
        3: ([10, 10, 11], [10, 11, 10, 11]),
        4: ([10, 11, 11, 10], [10, 11, 10, 11, 10]),
        5: ([10, 11, 10, 10, 11], [10, 11, 10, 11, 10, 11]),
        6: ([10, 11, 10, 11, 11, 10], [10, 11, 10, 11, 10, 11, 10]),
        7: ([10, 11, 10, 11, 10, 10, 11], [10, 11, 10, 11, 10, 11, 10, 11]),
    }
    for depth, (chain, late) in expected_depths.items():
        cases = EXPECTED_SETS[f"interrupt-depth-{depth}"]
        assert set(cases) == set(DEPTH_CHAIN_CASES + DEPTH_LATE_CASES)
        assert all(cases[name] == chain for name in DEPTH_CHAIN_CASES)
        assert all(cases[name] == late for name in DEPTH_LATE_CASES)
    for argv in ([], ["qemu"], ["--expect=interrupt-depth-3"]):
        try:
            command_args(argv)
        except RuntimeError:
            continue
        raise AssertionError(f"bad command line was accepted: {argv}")
    print("trace parser self-test passed")


def split_expect(argv):
    """Takes a leading --expect=<set> off argv before QEMU's own arguments."""
    if argv and argv[0].startswith("--expect="):
        name = argv[0][len("--expect="):]
        if name not in EXPECTED_SETS:
            raise RuntimeError(f"unknown expected set: {name!r}")
        return EXPECTED_SETS[name], argv[1:]
    return EXPECTED, argv


def command_args(argv):
    """Validate the wrapper arguments after the optional expected-set flag."""
    expected, qemu_argv = split_expect(argv)
    if len(qemu_argv) < 3:
        raise RuntimeError("usage: check_interrupt_completion.py [--expect=SET] QEMU QEMU-OPTION ELF")
    return expected, qemu_argv


def check(qemu, qemu_args, elf, expected):
    with tempfile.TemporaryDirectory() as directory:
        trace = Path(directory) / "interrupt.trace"
        command = [
            qemu,
            *qemu_args[:-1],
            "-trace",
            f"enable=memory_region_ops_write,file={trace}",
            qemu_args[-1],
            str(elf),
        ]
        result = subprocess.run(command, capture_output=True, timeout=25)
        output = result.stdout.decode("utf-8", errors="replace")
        if result.returncode != 0:
            raise RuntimeError(f"QEMU exited {result.returncode}: {output}\n{result.stderr.decode(errors='replace')}")
        if ":FAIL:" in output or not trace.is_file():
            raise RuntimeError(f"interrupt suite failed or trace is missing: {output}")
        seen = parse_trace(trace.read_text(encoding="utf-8").splitlines(), expected)
        print(f"checked {len(seen)} passing interrupt cases and PLIC EOI writes")


if __name__ == "__main__":
    try:
        if sys.argv[1:] == ["--self-test"]:
            self_test()
        else:
            expected, argv = command_args(sys.argv[1:])
            check(argv[0], argv[1:-1], argv[-1], expected)
    except (IndexError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
