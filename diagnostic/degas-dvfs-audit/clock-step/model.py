"""Offline specification only. No device access, sysreg access or MMIO backend.

Input snapshots represent actual register reads, not normalized PMU state.
The returned command plan is for review; this module never executes it.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class Window:
    pmcr: int
    pmcntenset: int
    pmccfiltr: int
    current_el: int
    mpidr_before: int
    mpidr_after: int
    counter_before: int
    counter_after: int
    timer_before: int
    timer_after: int
    timer_hz: int


def active_hz(w: Window) -> int:
    # Deliberately narrow supported inherited configuration. Never repair it.
    if w.current_el != 1 or w.mpidr_before != w.mpidr_after:
        raise ValueError('EL or PE changed/unqualified')
    if not w.pmcr & 1 or not w.pmcntenset & (1 << 31):
        raise ValueError('counter disabled')
    if not w.pmcr & (1 << 6):
        raise ValueError('LC=0 overflow/divider configuration not supported')
    # LC=1 means D does not divide the cycle count. Never multiply by64 here.
    if w.pmcr & ((1 << 5) | (1 << 9)):
        raise ValueError('DP/FZO prohibition/freeze configuration unqualified')
    if w.pmccfiltr != 0:
        raise ValueError('inherited filtering not supported by narrow EL1 gate')
    ticks = w.timer_after - w.timer_before
    if w.timer_hz <= 0 or not w.timer_hz // 200 <= ticks <= w.timer_hz // 20:
        raise ValueError('active sample must last5..50ms')
    cycles = (w.counter_after - w.counter_before) & ((1 << 64) - 1)
    hz = cycles * w.timer_hz // ticks
    if not 1_235_000_000 <= hz <= 1_365_000_000:
        raise ValueError('baseline is not within5percent of1300MHz')
    return hz


def plan(*, mode: int, enable: int, request: int, ready: int,
         lut_khz: list[int], windows: list[Window], pmu_reads_completed: bool,
         domain0_pe_confirmed: bool, thermal_gate: bool,
         thermal_evidence: str, ceiling_khz: int) -> list[tuple[str, int]]:
    """Raise before proposing any write unless every precondition is satisfied.

    pmu_reads_completed is false after any handled trap; a failed read must
    never be replaced with zero/default data. Backend must reread PMU metadata
    around each window and reject changes before constructing these inputs.
    """
    if not pmu_reads_completed or not domain0_pe_confirmed:
        raise ValueError('PMU read failed or CPU is not qualified domain0 PE')
    if (mode, enable, request, ready) != (1, 0x101, 9, 3):
        raise ValueError('boot controller precondition changed')
    if len(lut_khz) != 18 or lut_khz[9] != 1_300_000:
        raise ValueError('original request cannot be decoded unambiguously')
    if any(a <= b for a, b in zip(lut_khz, lut_khz[1:])):
        raise ValueError('LUT is not strictly descending')
    higher = sorted(f for f in lut_khz if f > 1_300_000)
    if not higher or higher[0] != 1_400_000:
        raise ValueError('next stock OPP is not1400MHz')
    if not thermal_gate or not thermal_evidence or ceiling_khz < 1_400_000:
        raise ValueError('fresh temperature/ceiling qualification missing')
    if len(windows) != 3:
        raise ValueError('need3active baseline windows')
    for window in windows:
        active_hz(window)
    if len({(w.pmcr, w.pmcntenset, w.pmccfiltr,
             w.current_el, w.mpidr_before) for w in windows}) != 1:
        raise ValueError('PMU metadata changed')
    # Symbolic specification. Poll/measure failures after enable must enter
    # restore; high-frequency residency bounded independently by timer.
    return [('domain0_enable', 1), ('poll_ready_timeout_us', 300_000),
            ('domain0_request_khz', 1_300_000), ('measure_hold_us', 30_000),
            ('domain0_request_khz', 1_400_000), ('measure_step_us', 30_000),
            ('domain0_request_khz', 1_300_000), ('measure_restore_us', 30_000)]


def recovery_after_enable() -> list[tuple[str, int]]:
    # Restore frequency encoding, never restore the boot index9 in kHz mode.
    # Leave documented enable1 for BootAndroid; do not put unexplained bit8 back.
    return [('domain0_request_khz', 1_300_000)]
