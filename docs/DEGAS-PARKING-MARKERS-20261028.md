# Parking release-marker diagnostic, 2026-10-28

Three independent reviews found no proven ACPI table corruption or mandatory missing dc civac/isb. One protocol mismatch is corrected: GICv3 noncompat mailbox release accepts Processor ID0 only.

Parking.S records SCTLR_EL1, CurrentEL, mailbox ID, target and release flag in firmware-owned offsets c10..c30; OS-owned16..2047 remains untouched. Seven 16x16 native-framebuffer squares at x16, y512+64*AP affinity are outside the diagnostic Windows viewport. White means entered parking; green means OS-release branch with SCTLR M/C/I zero; amber means one of those bits is set. No new architectural state writes or GIC reads. Visibility can be unreliable if inherited cache state is unexpected, so absent markers alone are not conclusive.

This is a diagnostic, not a demonstrated KMODE fix. It does not patch Windows, PRCB pointers, CPU clocks, memory attributes or GIC setup.

Unicorn: 91 scenarios plus code-budget test passed. Source/code hashes in artifacts/degas/cpu-audit/parking-v2-code.json. Linked firmware must match tested296bytes before flashing.
