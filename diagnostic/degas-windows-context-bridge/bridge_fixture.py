"""OFFLINE ONLY: exact-build native Windows AP prefix experiment.

No file patcher, device command, memory scanner, or firmware integration exists
here. All mutations are bytearrays supplied by a host test. The reserved base
is synthetic. A future UEFI integration must independently establish allocation
ownership, loaded-image identity, cache publication, and integrity acceptance.
"""
from pathlib import Path
import hashlib
import struct
import uuid

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'evidence/raw/2026-10-29-degas-active-boot-identity/ntoskrnl.exe'
SOURCE_SHA256 = '5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c'
PREFERRED_BASE = 0x140000000
IMAGE_SIZE = 0x124a000
TEMPLATE_START, TEMPLATE_END = 0x2000a0, 0x200220
PREFIX_START, PREFIX_END = 0x2000c0, 0x2000f8
TEMPLATE_SHA256 = '24f05c67ffdf1b8b4c714dd1c71009de210f8432670a6743c0524567b89664ef'
PREFIX_ORIGINAL = bytes.fromhex(
    '21423bd54100383700000014df4103d5e1ffff10e2ff81d22100228a'
    '02800091240440f9050080d2850005aaa5002091252018d5df3f03d5')
CODEVIEW = bytes.fromhex('52534453c06cd400a1e2483e6ac6c224799cfc23010000006e746b726e6c6d702e70646200')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def headers(data):
    """Bounded parser; caller already owns and bounds this single allocation."""
    def need(offset, length):
        if offset < 0 or length < 0 or offset + length > len(data):
            raise ValueError('PE structure exceeds supplied allocation')
    need(0, 0x40)
    if data[:2] != b'MZ':
        raise ValueError('DOS signature')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    need(pe, 24)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('PE signature')
    machine, count = struct.unpack_from('<HH', data, pe + 4)
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    need(opt, optional_size)
    if machine != 0xaa64 or optional_size < 0xf0 or struct.unpack_from('<H', data, opt)[0] != 0x20b:
        raise ValueError('ARM64 PE32+ expected')
    size, header_size = struct.unpack_from('<II', data, opt + 56)
    need(opt + optional_size, count * 40)
    sections = []
    for i in range(count):
        at = opt + optional_size + i * 40
        virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, at + 8)
        flags = struct.unpack_from('<I', data, at + 36)[0]
        if rva + max(virtual_size, raw_size) > size:
            raise ValueError('section exceeds image')
        sections.append(dict(name=bytes(data[at:at + 8]), rva=rva,
                             virtual_size=virtual_size, raw_size=raw_size,
                             raw=raw, flags=flags))
    return dict(pe=pe, opt=opt, size=size, header_size=header_size, sections=sections)


def source_mapped():
    raw = SOURCE.read_bytes()
    if sha(raw) != SOURCE_SHA256:
        raise ValueError('exact saved active kernel SHA256 mismatch')
    h = headers(raw)
    out = bytearray(h['size'])
    out[:h['header_size']] = raw[:h['header_size']]
    for s in h['sections']:
        if s['raw'] + s['raw_size'] > len(raw):
            raise ValueError('file section outside source')
        out[s['rva']:s['rva'] + s['raw_size']] = raw[s['raw']:s['raw'] + s['raw_size']]
    validate_loaded(out, PREFERRED_BASE)
    return out


def validate_loaded(image, actual_base):
    """Validate candidate, NOT discover it. No unsafe reads or memory scan.

    actual_base is the image RELOCATION VIRTUAL BASE, not its physical backing
    allocation. A physical EBS locator cannot substitute its physical candidate
    address here. The high virtual value is only checked numerically and never
    dereferenced. RSDS alone is not a cryptographic identity check; full template
    and related byte guards follow. Preferred base is for offline raw fixtures.
    """
    h = headers(image)
    if h['size'] != IMAGE_SIZE or len(image) != IMAGE_SIZE:
        raise ValueError('exact SizeOfImage/allocation required')
    text = h['sections'][7]
    if text != dict(name=b'.text\0\0\0', rva=0x200000, virtual_size=0x46a794,
                    raw_size=0x46a800, raw=0xff600, flags=0x68000020):
        raise ValueError('exact .text ownership mismatch')
    dr, size = struct.unpack_from('<II', image, h['opt'] + 112 + 6 * 8)
    if size % 28 or dr + size > len(image):
        raise ValueError('debug directory bounds')
    found = []
    for at in range(dr, dr + size, 28):
        kind, length, rva = struct.unpack_from('<III', image, at + 12)
        if rva + length > len(image):
            raise ValueError('debug record bounds')
        if kind == 2:
            found.append(bytes(image[rva:rva + length]))
    if found != [CODEVIEW]:
        raise ValueError('exact RSDS GUID/age/name mismatch')
    expected_pointer = actual_base + 0x2001fc
    if struct.unpack_from('<Q', image, 0x200218)[0] != expected_pointer:
        raise ValueError('template DIR64 relocation mismatch')
    normalized = bytearray(image[TEMPLATE_START:TEMPLATE_END])
    struct.pack_into('<Q', normalized, 0x200218 - TEMPLATE_START, PREFERRED_BASE + 0x2001fc)
    if sha(normalized) != TEMPLATE_SHA256:
        raise ValueError('full relocation-normalized native template mismatch')
    if bytes(image[PREFIX_START:PREFIX_END]) != PREFIX_ORIGINAL:
        raise ValueError('native prefix mismatch')
    # No relocations occur in these guards.
    for at, expected in [(0x47f8f8, '020d40f9'), (0x49834c, 'e30302aa'),
                         (0x49835c, '220040f9'), (0x6261d8, '030000d4'),
                         (0x4981b0, '09810891'), (0x4981b8, '13810291')]:
        if bytes(image[at:at + 4]).hex() != expected:
            raise ValueError('related native path guard mismatch at ' + hex(at))
    return h


def validate_physical_candidate(image, physical_base):
    """Host model of an already bounded EBS physical image candidate.

    The sole template DIR64 target supplies the relocation virtual base. Full
    normalized template/PE checks must still succeed. Inference never implies
    that this high VA is mapped by UEFI, and nothing dereferences it. This
    function does not discover allocations or establish memory-map coverage.
    """
    if physical_base <= 0 or physical_base & 0xfff or physical_base + len(image) > 1 << 64:
        raise ValueError('bounded page-aligned physical allocation required')
    headers(image)
    if len(image) != IMAGE_SIZE:
        raise ValueError('complete candidate size required')
    virtual_base = struct.unpack_from('<Q', image, 0x200218)[0] - 0x2001fc
    if virtual_base < 0 or virtual_base >> 48 != 0xffff or virtual_base & 0xfff:
        raise ValueError('canonical high aligned kernel relocation VA required')
    validate_loaded(image, virtual_base)
    return dict(physical_base=physical_base, virtual_base=virtual_base,
                template_physical=physical_base + TEMPLATE_START,
                prefix_physical=physical_base + PREFIX_START)


def prefix_words(reserved_base):
    """8 contiguous 4KiB pages, CPU affinity1 values0..7, below4GiB.

    This topology is an external experiment precondition. A three-bit UBFX is
    deliberately NOT a general MPIDR parser. UEFI must validate the complete
    affinity map before publication and must abort on any incompatible MADT.
    """
    if reserved_base <= 0 or reserved_base & 0xfff or reserved_base + 0x8000 > 1 << 32:
        raise ValueError('owned aligned eight-page low-memory allocation required')
    return [0xd53b4221,        # mrs x1, daif
            0x36380001,        # tbz w1,#7,self: retain infinite failure loop
            0xd50341df,        # msr daifset,#1
            0xd53800a0,        # mrs x0,mpidr_el1
            0xd3482800,        # ubfx x0,x0,#8,#3
            0xd2800002 | ((reserved_base & 0xffff) << 5),
            0xf2a00002 | (((reserved_base >> 16) & 0xffff) << 5),
            0x8b003040,        # add x0,x2,x0,lsl#12
            0x91008002,        # add x2,x0,#0x20
            0x90000001,        # adrp x1,current page (position independent)
            0xf9400424,        # ldr x4,[x1,#8]
            0x91200085,        # add x5,x4,#0x800
            0xd5182025,        # msr ttbr1_el1,x5
            0xd5033fdf]        # isb


def patch_fixture(image, actual_base, reserved_base):
    """Returns a separate host bytearray. Never writes SOURCE or any path."""
    validate_loaded(image, actual_base)
    out = bytearray(image)
    out[PREFIX_START:PREFIX_END] = struct.pack('<14I', *prefix_words(reserved_base))
    assert out[:PREFIX_START] == image[:PREFIX_START]
    assert out[PREFIX_END:] == image[PREFIX_END:]
    return out


def identity_summary():
    return dict(source=str(SOURCE), sha256=SOURCE_SHA256,
                size_of_image=hex(IMAGE_SIZE),
                codeview_guid=str(uuid.UUID(bytes_le=CODEVIEW[4:20])).upper(),
                codeview_age=struct.unpack_from('<I', CODEVIEW, 20)[0],
                template_rva=[hex(TEMPLATE_START), hex(TEMPLATE_END)],
                template_sha256=TEMPLATE_SHA256,
                prefix_rva=[hex(PREFIX_START), hex(PREFIX_END)],
                persistent_text=True, deployable=False)
