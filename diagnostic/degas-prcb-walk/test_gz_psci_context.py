"""Pinned GZ CPU_ON context-loss reproduction and NON-DEPLOYABLE fix model.

Runs the original GZ instructions on a host; no firmware image is modified.
Calls outside the selected handler are modeled, including native SMC and
memset. The model injects a context load/store after memset using a Unicorn
hook. It is a behavioral candidate, NOT an instruction patch or flash image.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/vendor/python'))
sys.path.insert(0, str(ROOT / 'artifacts/degas/cpu-audit/test-deps'))
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

SOURCE = ROOT / 'evidence/raw/2026-10-24-degas-return/gz_b.img'
SOURCE_SHA = '011c07ed744d69a7b2760a3fb2bdd727327bec08bc844dd4177086e4ce04ee0e'
PAYLOAD_OFFSET, PAYLOAD_SIZE, BASE = 0x200, 0x1b77c0, 0x38000000
START, END, MODEL_POINT = BASE + 0x3d6d8, BASE + 0x3d03c, BASE + 0x3d764
RAM, RAM_SIZE = 0x43000000, 0x40000
CALLER, OFFSETS, BOOTARG = RAM, RAM + 0x2000, RAM + 0x3000
VCPU_BASE, ONLINE, SECOND_BASE = RAM + 0x10000, RAM + 0x20000, RAM + 0x28000
FP, ENTRY, NATIVE_ENTRY = RAM + 0x3e000, 0x44000000, 0x45000000
OUT = ROOT / 'artifacts/degas/cpu-audit/gz-context-audit'
RESULTS = []


def q(value):
    return struct.pack('<Q', value)


def run_case(context, cpu, model):
    image = SOURCE.read_bytes()
    assert hashlib.sha256(image).hexdigest() == SOURCE_SHA
    payload = image[PAYLOAD_OFFSET:PAYLOAD_OFFSET + PAYLOAD_SIZE]
    assert len(payload) == PAYLOAD_SIZE
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE, 0x200000)
    uc.mem_write(BASE, payload)
    uc.mem_map(RAM, RAM_SIZE)
    offset, mpidr = 0x4000, cpu << 8
    target, secondary = VCPU_BASE + offset, SECOND_BASE + offset
    canary = b'\xa5' * 0x1400
    uc.mem_write(target, canary)
    uc.mem_write(secondary, canary)
    caller = q(0xc4000003) + q(mpidr) + q(ENTRY) + q(context) + b'\x5a' * 32
    uc.mem_write(CALLER, caller)
    for address, value in [(0x381b73a8, BOOTARG), (0x381b7200, OFFSETS),
                           (0x381b7560, VCPU_BASE), (0x381b70a0, ONLINE),
                           (0x381b7498, SECOND_BASE), (OFFSETS + cpu * 8, offset)]:
        uc.mem_write(address, q(value))
    uc.mem_write(ONLINE + offset, b'\x01')  # take native CPU_ON forwarding path
    uc.reg_write(UC_ARM64_REG_X20, CALLER)
    uc.reg_write(UC_ARM64_REG_X29, FP)
    uc.reg_write(UC_ARM64_REG_SP, FP - 0x100)
    uc.reg_write(UC_ARM64_REG_X0, 0xc4000003)
    calls, reached, injected = [], False, False
    restored, exit_state = False, {}

    def get(n):
        return uc.reg_read(globals()['UC_ARM64_REG_X' + str(n)])

    def stub_return(value=0):
        uc.reg_write(UC_ARM64_REG_X0, value)
        uc.reg_write(UC_ARM64_REG_PC, get(30))

    def hook(_, pc, size, data):
        nonlocal reached, injected, restored
        if pc == END:
            reached = True
            uc.emu_stop()
        elif pc == BASE + 0x623c8:  # logging
            stub_return()
        elif pc == BASE + 0x3150c:  # helper outside selected window
            stub_return()
        elif pc == BASE + 0x3bbf8:  # MPIDR -> logical CPU index
            assert get(0) == mpidr
            stub_return(cpu)
        elif pc == BASE + 0x5c514:  # provider's native warmboot entry
            assert get(0) == BOOTARG
            stub_return(NATIVE_ENTRY)
        elif pc == BASE + 0x32e94:  # selected CPU currently OFF
            assert get(0) == cpu
            stub_return()
        elif pc == BASE + 0x62cdc:  # bounded modeled memset
            assert (get(0), get(1), get(2)) == (target + 0x1000, 0, 0x34c)
            uc.mem_write(get(0), b'\0' * get(2))
            stub_return(target + 0x1000)
        elif pc == MODEL_POINT and model:
            # MODEL ONLY: equivalent to loading original caller X3 then
            # storing it in the target guest X0 slot AFTER the zeroing.
            # No payload bytes are changed; no fitting patch is claimed.
            saved_context = bytes(uc.mem_read(CALLER + 0x18, 8))
            uc.mem_write(target + 0x1000, saved_context)
            injected = True
        elif pc == BASE + 0x1d280:  # AArch64 guest
            stub_return(1)
        elif pc == BASE + 0x1b510:  # native SMC: never executed on hardware
            args = [get(i) for i in range(4)]
            assert args == [0xc4000003, mpidr, NATIVE_ENTRY, 0]
            calls.append(args)
            stub_return()
        elif pc == BASE + 0x1c7c:  # modeled interrupt-mask system register
            uc.reg_write(UC_ARM64_REG_PC, pc + 4)
        elif pc == BASE + 0x1c98:  # actual MSR ELR_EL2 source register
            exit_state['pc'] = get(21)
            uc.reg_write(UC_ARM64_REG_PC, pc + 4)
        elif pc == BASE + 0x1c9c:  # actual MSR SPSR_EL2 source register
            exit_state['spsr'] = get(22)
            uc.reg_write(UC_ARM64_REG_PC, pc + 4)
        elif pc == BASE + 0x1cd4:  # stop BEFORE ERET; no guest code executed
            exit_state['x0'] = get(0)
            restored = True
            uc.emu_stop()

    uc.hook_add(UC_HOOK_CODE, hook)
    uc.emu_start(START, BASE + PAYLOAD_SIZE, count=1000)
    assert reached, 'handler did not reach its common return within instruction bound'
    expected = bytearray(canary)
    expected[0x1000:0x134c] = b'\0' * 0x34c
    struct.pack_into('<Q', expected, 0x1100, ENTRY)
    struct.pack_into('<I', expected, 0x1108, 0x3c5)
    struct.pack_into('<II', expected, 0x1368, cpu, 0)
    if model:
        struct.pack_into('<Q', expected, 0x1000, context)
    observed = bytes(uc.mem_read(target, len(canary)))
    assert observed == expected, 'unexpected target field changed'
    expected_secondary = bytearray(canary)
    struct.pack_into('<II', expected_secondary, 0x1368, cpu, 0)
    assert bytes(uc.mem_read(secondary, len(canary))) == expected_secondary
    assert bytes(uc.mem_read(CALLER, len(caller))) == q(0) + caller[8:]
    # Mock globals occupy the image's runtime data area. Executable bytes and
    # the actual source file must remain byte-for-byte original.
    assert bytes(uc.mem_read(BASE, 0x68000)) == payload[:0x68000], 'code bytes changed'
    assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == SOURCE_SHA
    assert calls and injected == model
    # Independently execute the exact final register-restore instructions.
    # The world switch's selection of this target+1000 stack is supplied by
    # the fixture; this is not an emulation of the intervening scheduler.
    uc.reg_write(UC_ARM64_REG_SP, target + 0x1000)
    uc.emu_start(BASE + 0x1c7c, BASE + PAYLOAD_SIZE, count=100)
    assert restored and exit_state == {
        'x0': context if model else 0, 'pc': ENTRY, 'spsr': 0x3c5,
    }
    result = {
        'mode': 'MODEL_ONLY_NOT_DEPLOYABLE' if model else 'ORIGINAL_INSTRUCTIONS',
        'cpu_index': cpu, 'mpidr': hex(mpidr), 'input_context': hex(context),
        'guest_x0': hex(struct.unpack_from('<Q', observed, 0x1000)[0]),
        'guest_pc': hex(struct.unpack_from('<Q', observed, 0x1100)[0]),
        'guest_spsr': hex(struct.unpack_from('<I', observed, 0x1108)[0]),
        'native_cpu_on_arguments': [hex(v) for v in calls[0]],
        'untouched_fields_and_caller_arguments_verified': True,
        'payload_unchanged': True,
        'actual_final_restore': {k: hex(v) for k, v in exit_state.items()},
    }
    RESULTS.append(result)
    return result


class GzPsciContextTest(unittest.TestCase):
    def test_original_and_minimum_behavioral_model(self):
        for cpu in (1, 7):
            for context in (0, 1, 0x12345678, 0x1122334455667788, 0xffffffffffffffff):
                with self.subTest(cpu=cpu, context=hex(context)):
                    original = run_case(context, cpu, False)
                    model = run_case(context, cpu, True)
                    self.assertEqual(int(original['guest_x0'], 16), 0)
                    self.assertEqual(int(model['guest_x0'], 16), context)


def write_artifacts():
    image = SOURCE.read_bytes()
    payload = image[PAYLOAD_OFFSET:PAYLOAD_OFFSET + PAYLOAD_SIZE]
    OUT.mkdir(parents=True, exist_ok=True)
    md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    lines = ['Original bytes only; MODEL hook does not alter this disassembly.']
    for lo, hi in [(0x3d604, 0x3d614), (0x3d6d8, 0x3d7d4), (0x1c7c, 0x1cd8)]:
        lines.append('')
        for ins in md.disasm(payload[lo:hi], BASE + lo):
            lines.append(f'{ins.address:08x}: {ins.bytes.hex()} {ins.mnemonic} {ins.op_str}')
    (OUT / 'gz-cpu-on-original.asm').write_text('\n'.join(lines) + '\n')
    report = {
        'source': str(SOURCE.relative_to(ROOT)), 'source_sha256': SOURCE_SHA,
        'payload_offset': hex(PAYLOAD_OFFSET), 'payload_size': hex(PAYLOAD_SIZE),
        'payload_sha256': hashlib.sha256(payload).hexdigest(), 'linked_base': hex(BASE),
        'instruction_window': [hex(START), hex(BASE + 0x3d7d4)],
        'model_hook_address': hex(MODEL_POINT), 'firmware_modified': False,
        'scope': 'Host execution of actual selected handler and final register-restore instructions; external helper calls and world-switch stack selection modeled. Stops before ERET. No CPU/MMU/coherency/secure-world hardware validation.',
        'model_status': 'Behavioral load caller+18 / store target+1000 after memset only. NOT a deployable binary patch, provider configuration, or authorization to modify GZ.',
        'cases': RESULTS,
    }
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(GzPsciContextTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if result.wasSuccessful():
        write_artifacts()
    raise SystemExit(not result.wasSuccessful())
