"""Host-only publication model, not a firmware or Windows CPU_ON interceptor.

`events` specifies required cache/barrier operations; Python performs no cache
maintenance. Native CPU_ON is modeled by its four arguments and result only.
"""
from dataclasses import dataclass
import struct
from threading import Lock

MASK64 = (1 << 64) - 1
AFFINITY_MASK = 0xff00ffffff
CONFIG_MAGIC = 0x4150435458425231
TABLE_MAGIC = 0x4150434254424c31
SLOT_MAGIC = 0x41504342534c5431
EMPTY, RESERVED, READY, CLAIMED, CANCELLED = range(5)
PAGE = 4096
HEADER = STRIDE = 128
# Mapping requirements for a future publisher, not page tables installed here.
# MAIR index0=Normal-NC(0x44), index1=Normal-WB(0xff); AF, Inner Shareable.
CODE_PTE = 3 | (1 << 10) | (3 << 8) | (2 << 6) | (1 << 54) # EL1 RO RX
DATA_PTE = 3 | (1 << 10) | (3 << 8) | (1 << 2) | (1 << 53) | (1 << 54)
MAIR = 0xff44


def cpu_index(mpidr):
    if not isinstance(mpidr, int) or not 0 <= mpidr <= MASK64:
        raise ValueError('MPIDR must be a uint64')
    affinity = mpidr & AFFINITY_MASK
    if affinity & 0xff or not 1 <= affinity >> 8 <= 7:
        raise ValueError('only confirmed AP affinities 0x100..0x700 are accepted')
    return affinity >> 8


@dataclass(frozen=True)
class NativeCall:
    cpu: int
    epoch: int
    arguments: tuple


class PublicationModel:
    """Single boot epoch. READY descriptors are immutable and never recycled.

    A failed CPU_ON cancels the slot permanently. Starting a new epoch requires
    an external proof that every target is OFF and no CPU_ON is outstanding.
    A boolean supplied to reset() represents that proof, not a hardware check.
    """
    def __init__(self, epoch=1, code_pa=0x43000000, table_pa=0x43010000,
                 entry_low=0x40480000, entry_high=0x44440000, line_bytes=64):
        if not 1 <= epoch <= MASK64:
            raise ValueError('epoch must be a nonzero uint64')
        if any(pa % PAGE or not PAGE <= pa < (1 << 36) for pa in (code_pa, table_pa)):
            raise ValueError('code and table must be allocated aligned PA36 pages')
        if code_pa == table_pa:
            raise ValueError('separate RX code and XN data pages required')
        if not 0 < entry_low < entry_high <= (1 << 36):
            raise ValueError('caller must bound a physical executable RAM span')
        if line_bytes not in (16, 32, 64, 128):
            raise ValueError('slot isolation requires supported D-cache line <=128')
        self.epoch, self.code_pa, self.table_pa = epoch, code_pa, table_pa
        self.entry_low, self.entry_high, self.line_bytes = entry_low, entry_high, line_bytes
        self.lock, self.events, self.pending, self.used = Lock(), [], {}, set()
        self.table = bytearray(PAGE)
        struct.pack_into('<QQ', self.table, 0, TABLE_MAGIC, epoch)
        self._clean(0, PAGE)
        self.events.append(('dsb_sy',))

    def _clean(self, offset, size):
        start = offset & -self.line_bytes
        end = (offset + size + self.line_bytes - 1) & -self.line_bytes
        for line in range(start, end, self.line_bytes):
            self.events.append(('dc_cvac', self.table_pa + line))

    def config(self):
        return struct.pack('<8Q', CONFIG_MAGIC, self.epoch, self.table_pa,
                           self.entry_low, self.entry_high, 0, 0, 0)

    def publish(self, mpidr, entry, context):
        cpu = cpu_index(mpidr)
        if not isinstance(context, int) or not 0 <= context <= MASK64:
            raise ValueError('context must be uint64, including zero')
        if not (self.entry_low <= entry < self.entry_high) or entry & 3:
            raise ValueError('entry must be aligned in the caller-approved RAM span')
        if self.code_pa <= entry < self.code_pa + PAGE:
            raise ValueError('entry cannot recurse into bridge code/config')
        with self.lock:
            if cpu in self.used:
                raise RuntimeError('duplicate, outstanding or consumed CPU slot in this epoch')
            self.used.add(cpu)
            offset = HEADER + cpu * STRIDE
            struct.pack_into('<6Q', self.table, offset, RESERVED, cpu << 8,
                             self.epoch, entry, context, SLOT_MAGIC)
            self.events.append(('write_descriptor', cpu, offset, STRIDE))
            self._clean(offset, STRIDE) # every descriptor line visible before READY
            self.events.append(('dsb_sy',))
            struct.pack_into('<Q', self.table, offset, READY)
            self.events.append(('stlr_ready', cpu))
            self._clean(offset, 8)
            self.events.append(('dsb_sy',))
            call = NativeCall(cpu, self.epoch, (0xc4000003, cpu << 8, self.code_pa, 0))
            self.pending[cpu] = call
            self.events.append(('native_cpu_on',) + call.arguments)
            return call

    def native_result(self, call, status):
        with self.lock:
            if self.pending.get(call.cpu) != call or call.epoch != self.epoch:
                raise RuntimeError('unknown or repeated native completion')
            del self.pending[call.cpu]
            offset = HEADER + call.cpu * STRIDE
            if status:
                state = struct.unpack_from('<Q', self.table, offset)[0]
                if state != READY:
                    raise RuntimeError('failed native call with observed AP claim: quarantine')
                struct.pack_into('<Q', self.table, offset, CANCELLED)
                self._clean(offset, 8)
                self.events.append(('dsb_sy',))
            return status # exact native failure is retained; never forge SUCCESS

    def reset(self, epoch, *, all_targets_off=False):
        with self.lock:
            if not all_targets_off or self.pending:
                raise RuntimeError('reset requires proven all-OFF and no outstanding calls')
            if not self.epoch < epoch <= MASK64:
                raise ValueError('epoch must advance without wrap')
            self.epoch = epoch
            self.table[:] = bytes(PAGE)
            struct.pack_into('<QQ', self.table, 0, TABLE_MAGIC, epoch)
            self.used.clear()
            self._clean(0, PAGE)
            self.events.append(('dsb_sy',))
            # Actual caller must also rebuild/publish immutable code config.
