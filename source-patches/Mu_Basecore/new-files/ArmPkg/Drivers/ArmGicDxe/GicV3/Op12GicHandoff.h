// Non-secure GICv3 view: disable group enables only. Preserve ARE (bit4),
// DS, and any other existing control state. Clearing ARE from 1 to 0 is
// architecturally UNPREDICTABLE unless the implementation makes it RAO/WI.
// Scope intentionally limited to the OP12 Windows ExitBootServices path.
STATIC VOID Op12GicDisableGroups (UINTN Distributor)
{
  MmioAnd32 (Distributor + ARM_GIC_ICDDCR, ~(UINT32)(BIT0 | BIT1));
  ArmDataSynchronizationBarrier ();
}
