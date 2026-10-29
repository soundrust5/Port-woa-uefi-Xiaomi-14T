# AP context display: enlarged layout

2026-10-28. This supersedes the tiny single-column layout.

The diagnostic image draws eight 64-bit values below the Windows viewport. Read the left column top-to-bottom as values 1–4, then the right column as values 5–8. Each hexadecimal glyph is 18 by 30 pixels, with a 24-pixel character advance. Columns start at X64 and X640. Rows start at Y2528, 2568, 2608 and 2648; the last glyph ends at Y2678, within the 2712-row native surface.

Values: jump target; virtual saved-state pointer; initial X18/TPIDR; restored X18; restored X0; SP; PC; TCR. The values are copied from AP1's mailbox into firmware-owned bytes before rendering. OS-owned data and branch arguments remain unchanged.

105 emulator scenarios and the code-size check pass. Tested code is 756 bytes, below the 1024-byte reserved budget. SHA-256: 0acbab46220997005ce88af09baf821e421d46c8d23de1026db93b21800b517f. Compare the tested bytes against the linked EFI before flashing.

This changes diagnostic presentation only. HAL 0x5C and KMODE 0x1E have both occurred after the Windows LocalId handshake was corrected. The exact-zero check was a real diagnostic regression but does not explain all HAL failures. The user reported HAL parameters 23, 4, FFFFF7B840001860, D twice; that tuple has not yet been mapped conclusively to a callsite in the saved kernel.
