package com.woa.helper.util

import org.junit.Assert.*
import org.junit.Test

class MountTableTest {
    private val target = "/mnt/Windows"
    @Test fun exactSourceAndTarget() {
        val table = "/dev/block/sda17 /mnt/Windows fuseblk ro,relatime 0 0\n/dev/block/sda1 /other fuseblk rw 0 0"
        assertTrue(MountTable.matches(table, "/dev/block/sda17", target, false))
        assertFalse(MountTable.matches(table, "/dev/block/sda1", target, false))
        assertFalse(MountTable.matches(table, "/dev/block/sda17", "/mnt/Window", false))
    }
    @Test fun writableFallbackIsNotSuccess() {
        assertFalse(MountTable.matches("/dev/block/sda17 /mnt/Windows fuseblk ro 0 0", "/dev/block/sda17", target, true))
        assertTrue(MountTable.matches("/dev/block/sda17 /mnt/Windows fuseblk rw,relatime 0 0", "/dev/block/sda17", target, true))
    }
    @Test fun occupiedTargetRetainsDifferentSource() {
        val table = "/dev/block/sda15 /mnt/Windows vfat rw 0 0"
        assertEquals("/dev/block/sda15", MountTable.atTarget(table, target)?.source)
        assertFalse(MountTable.matches(table, "/dev/block/sda17", target, true))
    }
    @Test fun malformedAndOtherTargetIgnored() {
        assertNull(MountTable.atTarget("bad\n/dev/block/sda17 /else fuseblk rw 0 0", target))
    }
}
