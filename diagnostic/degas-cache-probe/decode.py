"""Decode observed cache-maintenance geometry, never infer capacity/sharing."""
from pathlib import Path
import argparse
import json
import struct

MASK64 = (1 << 64) - 1
AFFINITY_MASK = 0xff00ffffff
MAGIC = int.from_bytes(b'DGCACHE1', 'little')
COMMIT = int.from_bytes(b'DONECACH', 'little')
RECORD_BYTES = 512
ENTRY_OFFSET, ENTRY_STRIDE, MAX_ENTRIES = 256, 16, 14
STATUS = {0: 'complete', 1: 'in_progress', 2: 'bad_header', 3: 'wrong_cpu',
          4: 'wrong_el', 5: 'unsafe_ap_entry_state', 6: 'unsupported_ccidx',
          7: 'reserved_clidr_type', 9: 'cpu_off_returned', 10: 'unsafe_cpu_off_target'}
PHASE = {0: 'not_entered', 1: 'entered', 2: 'read_daif', 3: 'read_currentel',
         4: 'read_mpidr', 5: 'read_midr', 6: 'read_sctlr', 7: 'read_mmfr2',
         8: 'read_clidr', 9: 'read_original_csselr', 0x30: 'restore_csselr',
         0x31: 'csselr_restored', 0x32: 'restore_daif', 0x33: 'leaf_completed',
         0x3f: 'qualify_cpu_off_target', 0x40: 'cpu_off_requested',
         0x41: 'cpu_off_returned', 0x42: 'cpu_off_target_rejected'}


def uint64(value):
    if not isinstance(value, int) or not 0 <= value <= MASK64:
        raise ValueError('uint64 required')
    return value


def make_record(cpu, epoch, mode=None):
    if not isinstance(cpu, int) or not 0 <= cpu <= 7:
        raise ValueError('confirmed MT6897 CPU index 0..7 required')
    if not uint64(epoch):
        raise ValueError('nonzero attempt epoch required')
    mode = int(cpu != 0) if mode is None else mode
    if mode not in (0, 1):
        raise ValueError('mode0=BSP leaf, mode1=AP CPU_OFF wrapper')
    record = bytearray(RECORD_BYTES)
    struct.pack_into('<5Q', record, 0, MAGIC, RECORD_BYTES, epoch, cpu << 8, mode)
    return record


def selectors(clidr):
    """Only architecturally present selectors. Ignore all levels after Ctype0."""
    uint64(clidr)
    result = []
    for index in range(7):
        kind = (clidr >> (index * 3)) & 7
        if kind == 0:
            break
        if kind > 4:
            raise ValueError('reserved CLIDR Ctype at level ' + str(index + 1))
        for ind in ((0, 1) if kind == 3 else (1,) if kind == 1 else (0,)):
            result.append(dict(level=index + 1, kind='instruction' if ind else
                               'unified' if kind == 4 else 'data',
                               selector=(index << 1) | ind))
    return result


def decode_ccsidr(raw, ccidx):
    uint64(raw)
    if ccidx == 0:
        if raw >> 32:
            raise ValueError('legacy CCSIDR upper32 RES0 bits are nonzero')
        ways, sets = ((raw >> 3) & 0x3ff) + 1, ((raw >> 13) & 0x7fff) + 1
        ignored = (raw >> 28) & 15 # architecturally UNKNOWN; not policy evidence
    elif ccidx == 1:
        if raw & 0xff000000ff000000:
            raise ValueError('extended CCSIDR RES0 fields are nonzero')
        ways, sets = ((raw >> 3) & 0x1fffff) + 1, ((raw >> 32) & 0xffffff) + 1
        ignored = None
    else:
        raise ValueError('reserved MMFR2.CCIDX encoding')
    line = 1 << ((raw & 7) + 4)
    return dict(raw=hex(raw), format='CCIDX64' if ccidx else 'legacy32',
                line_bytes=line, ways=ways, sets=sets,
                maintenance_span_bytes=line * ways * sets,
                legacy_unknown_bits31_28=ignored,
                physical_cache_capacity_bytes=None, shared_cpu_ids=None,
                qualification='Set/way maintenance geometry only; physical capacity and sharing unproved')


def phase_name(phase):
    if phase & 255 in (0x20, 0x21, 0x22):
        operation = {0x20: 'select_csselr', 0x21: 'read_ccsidr', 0x22: 'entry_stored'}[phase & 255]
        return operation + '(selector=' + str(phase >> 8) + ')'
    return PHASE.get(phase, 'unknown_phase_' + hex(phase))


def decode_record(data, *, expected_epoch=None, expected_cpu=None):
    if len(data) != RECORD_BYTES:
        raise ValueError('exact512byte private cache probe record required')
    words = struct.unpack('<64Q', data)
    if words[0] != MAGIC or words[1] != RECORD_BYTES or words[2] == 0:
        raise ValueError('record identity/header mismatch')
    mpidr = words[3]
    if mpidr & 255 or mpidr > 0x700 or words[4] not in (0, 1):
        raise ValueError('record topology/mode mismatch')
    if expected_epoch is not None and words[2] != expected_epoch:
        raise ValueError('stale record from another attempt')
    if expected_cpu is not None and mpidr != expected_cpu << 8:
        raise ValueError('unexpected record CPU affinity')
    if words[18] > MAX_ENTRIES:
        raise ValueError('entry count exceeds bounded record')
    committed = words[21] == COMMIT
    status = words[9]
    if status not in STATUS:
        raise ValueError('unknown record status')
    result = dict(epoch=words[2], requested_mpidr=hex(mpidr), cpu=mpidr >> 8,
                  mode='AP_CPU_OFF' if words[4] else 'BSP_LEAF',
                  phase=hex(words[8]), phase_name=phase_name(words[8]),
                  status=status, status_name=STATUS[status], committed=committed,
                  raw_mpidr=hex(words[10]), midr=hex(words[11]), current_el=words[12],
                  sctlr_el1=hex(words[13]), original_daif=hex(words[14]),
                  original_csselr=hex(words[15]), clidr_el1=hex(words[16]),
                  id_aa64mmfr2_el1=hex(words[17]), ccidx=(words[17] >> 20) & 15,
                  count=words[18], incoming_x0=hex(words[19]),
                  cpu_off_return=hex(words[20]), detail=hex(words[22]),
                  last_selector=words[24], geometry_complete=False,
                  cpu_off_proved=False,
                  scope='Raw identification capture only; no physical capacity, sharing or frequency inference')
    clidr = words[16]
    result['clidr_fields'] = dict(loc=(clidr >> 24) & 7, louu=(clidr >> 27) & 7,
                                  louis=(clidr >> 21) & 7, icb=(clidr >> 30) & 7,
                                  raw_ctypes=[(clidr >> (3 * i)) & 7 for i in range(7)])
    raw_entries = [dict(selector=words[32 + 2 * i], raw_ccsidr=hex(words[33 + 2 * i]))
                   for i in range(words[18])]
    result['raw_entries'] = raw_entries
    result['caches'] = []
    if committed and status == 0:
        if result['ccidx'] not in (0, 1):
            raise ValueError('successful result contains reserved CCIDX encoding')
        if words[12] != 4 or words[10] & AFFINITY_MASK != mpidr:
            raise ValueError('successful result has inconsistent EL/MPIDR')
        expected = selectors(clidr)
        if len(expected) != words[18]:
            raise ValueError('completed record misses or adds a cache selector')
        for wanted, actual in zip(expected, raw_entries):
            if wanted['selector'] != actual['selector']:
                raise ValueError('unexpected/duplicate CCSIDR selector')
            result['caches'].append(dict(**wanted, **decode_ccsidr(
                int(actual['raw_ccsidr'], 16), result['ccidx'])))
        result['geometry_complete'] = True
    return result


def monitor(data, *, expected_epoch, expected_cpu, elapsed_ns,
            timeout_ns=500_000_000, ap_off_observed=False):
    """Host model of caller deadline/qualification; never modifies AP memory.

    ap_off_observed must come from a real qualified provider, not phase/commit.
    Timeout forbids code/record reuse and OS continuation until reset/OFF proof.
    """
    if elapsed_ns < 0 or timeout_ns <= 0:
        raise ValueError('positive bounded timeout and nonnegative elapsed time required')
    result = decode_record(data, expected_epoch=expected_epoch, expected_cpu=expected_cpu)
    if result['committed'] and result['status'] not in (0, 1):
        return dict(state='failed_requires_off_or_reset', observation=result)
    if result['geometry_complete']:
        if result['mode'] == 'BSP_LEAF':
            return dict(state='complete_bsp', observation=result)
        if ap_off_observed and result['phase'] == '0x40':
            result['cpu_off_proved'] = True
            return dict(state='complete_ap_off', observation=result)
    if elapsed_ns >= timeout_ns:
        return dict(state='timeout_requires_reset', observation=result)
    return dict(state='pending', observation=result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--offset', type=lambda v: int(v, 0), default=0)
    parser.add_argument('--epoch', type=lambda v: int(v, 0))
    parser.add_argument('--cpu', type=int)
    args = parser.parse_args()
    data = args.capture.read_bytes()[args.offset:args.offset + RECORD_BYTES]
    print(json.dumps(decode_record(data, expected_epoch=args.epoch, expected_cpu=args.cpu), indent=2))
