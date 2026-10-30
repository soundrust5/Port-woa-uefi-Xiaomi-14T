"""Capture the installed conninfra ABI during one reversible Android Wi-Fi cycle."""
from pathlib import Path
import json,shlex,sys,time
from degas_full_windows_stage_source import shell

out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
base='/sys/kernel/tracing';group='degas_conn_abi';instance=base+'/instances/'+group
events=[
 ('p','power','consys_hw_pwr_on','current=%x0:u32 driver=%x1:u32 cfg=@g_platform_config:u32'),
 ('r','clock','consys_get_co_clock_type_mt6897','value=$retval:s32'),
 ('p','init','consys_init_atf_data_mt6897_atf','cfg=@g_platform_config:u32'),
 ('p','spi','connsys_spi_master_cfg_mt6897_atf','current=%x0:u32 next=%x1:u32'),
 ('r','spi_ret','connsys_spi_master_cfg_mt6897_atf','status=$retval:s32'),
 ('p','adie','connsys_a_die_cfg_mt6897_atf','current=%x0:u32 next=%x1:u32'),
 ('r','adie_ret','connsys_a_die_cfg_mt6897_atf','status=$retval:s32 id=@conn_hw_env:x32'),
 ('p','rfread','consys_spi_read_mt6897_atf','subsys=%x0:u32 address=%x1:x32'),
]
assert shell('getprop ro.product.device').strip()==b'degas'
wifi=shell('settings get global wifi_on').strip();bt=shell('settings get global bluetooth_on').strip()
assert wifi in (b'0',b'1',b'2') and bt in (b'0',b'1')
installed=[];changed=False
try:
 shell('mkdir '+shlex.quote(instance))
 for kind,name,symbol,args in events:
  line=f'{kind}:{group}/{name} conninfra:{symbol} {args}'
  shell('printf "%s\\n" '+shlex.quote(line)+' >> '+base+'/kprobe_events')
  installed.append(name)
 shell('echo 1 > '+instance+'/events/'+group+'/enable; echo 1 > '+instance+'/tracing_on')
 changed=True
 shell('svc wifi disable; svc bluetooth disable')
 time.sleep(6)
 shell('svc wifi enable')
 time.sleep(12)
 shell('echo 0 > '+instance+'/tracing_on')
 (out/'trace.txt').write_bytes(shell('cat '+instance+'/trace'))
 (out/'probe-profile.txt').write_bytes(shell('cat '+base+'/kprobe_profile'))
 (out/'dmesg.txt').write_bytes(shell('dmesg'))
finally:
 for name in reversed(installed):
  shell('echo 0 > '+instance+'/events/'+group+'/'+name+'/enable; printf "%s\\n" '+shlex.quote('-:'+group+'/'+name)+' >> '+base+'/kprobe_events')
 shell('rmdir '+shlex.quote(instance))
 if changed:
  shell('svc wifi '+('enable' if wifi!=b'0' else 'disable')+'; svc bluetooth '+('enable' if bt==b'1' else 'disable'))
 (out/'capture.json').write_text(json.dumps(dict(wifi_before=wifi.decode(),bluetooth_before=bt.decode(),
    settings_restore_requested=changed,probes_removed=installed,partitions_written=False),indent=2)+'\n')
print('Captured native ABI trace; temporary probes removed and radio state restoration requested.')
