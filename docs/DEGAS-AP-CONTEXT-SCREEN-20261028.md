# AP1 context screen capture, 2026-10-28

Current baseline v3: first AP marker green, remaining6white; KMODE C0000005 writeVA0 unchanged. This shows firmware reachedAP1release withSCTLR M/C/I0, not successful completion ofOSstartup. No provenrootcauseyet.

Nextimage displays8sixteen-digithexrows atnative x128,y2528+20*row, belowWindowsinset. Rows are: physicaljump target; virtualsavedstate pointer(mailbox10); initialX18/TPIDR(mailbox40); restoredCONTEXT.X18(3F8); CONTEXT.X0(368); restoredSP(460); restoredPC(468); savedTCR(E0). OnlyAP1renders. Capturebeforejump; OSownedbytesremainunchanged. NoWindowsbinarypatch,cachepolicychange,CPUclockchangeornewarchitecturalstatewrite.

Parser/handshakeusesWindowscompressedMPIDRLocalID(100..700), notzero. Oldexactzero guardwasadiagnosticregressionandisremoved. MADTCPUInterfaceNumberremains0perGICv3.

Testedcode692bytes, SHA2569dbc5d00a1bcf2349748b4d38337276882129476dbefd59cf47050c28ec01dc3. 105Unicornscenarios+budgettest, includingpixel-levelhexcomparisonandOSdata/registerpreservation.

AdditionalHALstartup/restartpathexistsinstaticcodebutnotprovedactiveinthisboot. Do notasserttwo-stageearlyprobeasestablishedrootcause. Runtimekernelbasealsoawaitsconfirmation.
