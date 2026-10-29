#!/system/bin/sh
# Read exactly one formatted regmap register per sample: MT6363 TOPSTATUS 0x1e.
# Nine-byte lines and continuous range 0..ffff verified on this device.
i=0
while [ "$i" -lt 600 ]; do
  printf '%s ' "$i"
  dd if=/sys/kernel/debug/regmap/0-04/registers bs=9 skip=30 count=1 2>/dev/null
  sleep 0.1
  i=$((i+1))
done
