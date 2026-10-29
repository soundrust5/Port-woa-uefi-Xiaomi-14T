"""Read-only evidence bundle for an independent CPU/PSCI/ACPI audit."""
from pathlib import Path
import hashlib,json,re,shutil,struct,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/DEGAS-CPU-AUDIT-20261028'
assert not OUT.exists(),'Refuse to overwrite an existing evidence bundle'
OUT.mkdir();sources={}
def sha(b):return hashlib.sha256(b).hexdigest()
def copy(source,target):
 p=ROOT/source;q=OUT/target;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
 sources[target]={'source':source,'sha256':sha(q.read_bytes())}
def write(target,text):
 p=OUT/target;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8')
def decode(p):
 result=subprocess.run(['wsl','-d','Ubuntu','--','/home/gavso/op12-acpica/usr/bin/iasl','-d','/mnt/d/'+p.relative_to(Path('D:/')).as_posix()],capture_output=True,text=True,encoding='utf-8',errors='replace')
 assert result.returncode==0,result.stdout+result.stderr
 p.with_suffix('.iasl-log.txt').write_text(result.stdout+result.stderr,encoding='utf-8')
for name,folder,log in [
 ('01-original-psci','artifacts/degas-smp-v1/tables','evidence/raw/2026-10-27-degas-smp-status-v2-result/console-ramoops-0'),
 ('02-corrected-psci','artifacts/degas-cpu-startup-v1/tables','evidence/raw/2026-10-27-degas-cpu-startup-v2-result/console-ramoops-0')]:
 base='01-ACPI/'+name
 for table in ['Madt','Fadt','Dsdt','Gtdt']:
  target=base+'/'+table+'.bin';copy(folder+'/'+table+'.bin',target);decode(OUT/target)
 copy(folder+'/DSDT.asl',base+'/DSDT.asl')
 copy(folder+'/manifest.json',base+'/generation-manifest.json')
 # Reconstruct exactly the MADT mutations recorded in runtime publication.
 text=(ROOT/log).read_text();data=bytearray((OUT/base/'Madt.bin').read_bytes())
 entries=re.findall(r'DEGAS SMP cpu=(\d+) mpidr=([\da-fA-F]+) gicr=([\da-fA-F]+) affinity_state=([\da-fA-F]+)',text)
 assert len(entries)==8 and 'DEGAS SMP enabled=8 fallback=0' in text
 for cpu,mpidr,gicr,state in entries:
  offset=44+80*int(cpu);assert struct.unpack_from('<Q',data,offset+68)[0]==int(mpidr,16)
  struct.pack_into('<I',data,offset+12,1);struct.pack_into('<Q',data,offset+60,int(gicr,16))
 data[9]=0;data[9]=-sum(data)&255;assert sum(data)&255==0
 p=OUT/base/'Madt.RUNTIME-RECONSTRUCTED.bin';p.write_bytes(data);decode(p)
 write(base+'/TABLE-NOTES.txt','Madt.bin is the generated template: secondary GICR bases are zero until DXE.\nMadt.RUNTIME-RECONSTRUCTED.bin is reconstructed from the matching retained runtime log; it is NOT a direct RAM dump.\nFadt.bin has template DSDT/XDSDT pointers; AcpiTableDxe fills those links at installation. ARM_BOOT_ARCH=1 (PSCI, SMC).\nNo separate type0x0e GICR range subtable is emitted: each type0x0b GICC has a runtime per-CPU redistributor base.\nOriginal variant CPU Interface Number=UID; corrected GICv3 variant CPU Interface Number=0, UID remains unique.\n')
 copy(log,base+'/matching-runtime-console.txt')

dt_path='evidence/raw/2026-10-25-degas-ufs-baseline-v2/live-fdt.json'
dt=json.loads((ROOT/dt_path).read_text());nodes=dt['nodes']
selected={p:n for p,n in nodes.items() if p=='/cpus' or p.startswith('/cpus/') or p in ['/interrupt-controller','/psci','/timer','/soc/gic500@c000000','/soc/gic-cpu@c400000','/soc/performance-controller@11bc00','/cpuhvfs@114400']}
write('02-DTB/selected-nodes.json',json.dumps(dict(source=dt_path,source_sha256=sha((ROOT/dt_path).read_bytes()),original_dtb_sha256=sha((ROOT/'evidence/raw/2026-10-25-degas-ufs-baseline-v2/live-fdt.dtb').read_bytes()),nodes=selected),indent=2))
def prop(key,v):
 b=bytes.fromhex(v['hex']);strings=v.get('strings')
 if not b:return key+';'
 if strings and all(s and all(32<=ord(c)<127 for c in s) for s in strings):return key+' = '+', '.join(json.dumps(s) for s in strings)+';'
 if len(b)%4==0:return key+' = <'+' '.join('0x'+b[i:i+4].hex() for i in range(0,len(b),4)) + '>;'
 return key+' = ['+' '.join(f'{x:02x}' for x in b)+'];'
lines=['// Lossless property rendering of selected original DT nodes.','// References may point outside this excerpt; use selected-nodes.json for exact paths/hex.']
for path,n in selected.items():
 lines+=['',f'// Original path: {path}',path.rsplit('/',1)[-1]+' {']
 lines+=['    '+prop(k,v) for k,v in n.items()];lines+=['};']
write('02-DTB/cpus-gic-psci-excerpts.dtsi','\n'.join(lines)+'\n')

paths=[
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/DegasAcpiDxe.c',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/DegasAcpiDxe.inf',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/AcpiTablesGenerated.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/CpuTopologyCore.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/Parking.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/Parking.S',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/Checkpoint.c',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/CheckpointDxe.inf',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/WindowsBoot.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/OsHandoff.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/BootWatchdog.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/BdsLifecycle.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/CpuProbe.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/CpuProbe.S',
 'mu/Platforms/Xiaomi/degasPkg/Include/Protocol/DegasCpuStatus.h',
 'mu/Platforms/Xiaomi/degasPkg/Library/MemoryMapLib/MemoryMapLib.c',
 'mu/Platforms/Xiaomi/degasPkg/degas.dsc',
 'mu/Mu_Basecore/MdePkg/Library/ArmSmcLib/AArch64/ArmSmc.S',
 'mu/Mu_Basecore/MdePkg/Include/Library/ArmSmcLib.h',
 'mu/Mu_Basecore/MdePkg/Include/IndustryStandard/ArmStdSmc.h',
 'mu/Mu_Basecore/ArmPkg/Drivers/TimerDxe/TimerDxe.c',
 'mu/Mu_Basecore/ArmPkg/Drivers/ArmGicDxe/GicV3/ArmGicV3Dxe.c',
 'mu/Mu_Basecore/ArmPkg/Drivers/ArmGicDxe/GicV3/Op12GicHandoff.h',
 'tools/generate_degas_acpi.py','tools/prepare_degas_windows_facts.py',
 'tools/build_degas_first_dxe.sh']
for p in paths:copy(p,'03-SOURCE/'+p)
for p in ['docs/DEGAS-SMP-20261027.md','docs/DEGAS-CPU-STARTUP-AUDIT-20261027.md','docs/DEGAS-CPU-PARKING-EXPERIMENT-20261027.md']:
 copy(p,'04-REPORTS/'+Path(p).name)
for name in ['degas-cpu-startup-v1-result-retry','degas-cpu-startup-v2-result','degas-parking-v1-bsod']:
 folder='evidence/raw/2026-10-27-'+name
 copy(folder+'/console-ramoops-0','05-LOGS/'+name+'.txt')
write('05-LOGS/WARNING-parking-bsod-log.txt','The parking-v1-bsod console is a later menu/END TEST boot, NOT the failed Windows attempt. It contains no PARKING READY or Windows handoff trace. No dump or visible 0x1E parameters were captured. Do not infer failure location from this stale console.\n')
for p in ['degas-smp-status-v2','degas-cpu-startup-v1','degas-cpu-startup-v2','degas-cpu-parking-v1']:
 copy('artifacts/'+p+'/boot.overlay.json','06-IMAGE-PROVENANCE/'+p+'.json')
write('README-RU.txt','''Xiaomi 14T / degas / MT6897 — пакет для независимого разбора CPU/PSCI, 28.10.2026.

Содержимое:
01-ACPI: исходные бинарные таблицы, DSDT.asl, дизассемблированные iASL .dsl.
Два PSCI-варианта: до и после исправления CPU Interface Number в GICC.
Madt.RUNTIME-RECONSTRUCTED — реконструкция по журналу с реальными GICR-адресами;
это не прямой дамп RAM. FADT здесь шаблон: DSDT/XDSDT-ссылки заполняет AcpiTableDxe.
Отдельной GICR-подтаблицы нет; адрес каждого redistributor передаётся в его GICC.

02-DTB: точные выбранные узлы из сохранённого родного DTB, с raw hex и читаемыми
свойствами. Основной GIC — /interrupt-controller (arm,gic-v3), не перепутать
с вспомогательными vendor-узлами /soc/gic500 и /soc/gic-cpu. Имена cpu@*
не являются MPIDR: использовать reg. PSCI method=smc.

03-SOURCE: текущие исходники DXE/ACPI, реальный вызов ArmCallSmc, CPU-пробник,
передача управления Windows и ExitBootServices, таймер/GIC shutdown.
Parking эксперимент включается только DEGAS_CPU_PARKING_EXPERIMENT=1 и явным
выбором Windows Boot Manager. По умолчанию он выключен. PSCI-ветка не стартует
AP перед Windows. Код SEC остаётся на BSP, PcdCoreCount=1 сам по себе не задаёт
число CPU Windows. В пакете нет полного дерева зависимостей для сборки.

04-REPORTS: прежние отчёты. Читайте их как историю экспериментов, не как
доказательство всех записанных гипотез. 05-LOGS: необработанные журналы.
06-IMAGE-PROVENANCE: хеши образов и параметры упаковки, без самих 64-МиБ образов.

Подтверждено:
- Windows ранее показала 1 логический процессор, интерфейс очень медленный.
- UEFI публикует 8 GICC, проверка GICR и PSCI_FEATURES проходит.
- Во втором CPU-пробнике ВСЕ 7 AP выполнили код, записали правильные MPIDR,
  MIDR и CNTFRQ=13МГц и вернулись в OFF.
- На всех AP входной X0 записан как 0 при ненулевом запрошенном context_id.
  Надпись «0 OF 7» была строгим счётчиком проверки ABI, не числом выполненных AP.
- Место потери контекста (обёртка/прошивка/иная причина) независимо не установлено.
- Эксперимент с parking закончился пользовательским BSOD 0x1E; параметров нет.
  Доступный журнал этого эксперимента перезаписан последующим меню/END TEST.

Не доказано: что именно потеря контекста оставляет Windows на одном ядре;
что BSOD вызван MMU/прерыванием; что реальные частоты ограничены 1300МГц.
900–1300МГц — показание Task Manager, независимого измерения нет.

Просьба: проверить аргументы CPU_ON, получаемый AP контекст, FADT/MADT/DSDT,
и обе альтернативные передачи управления. Разделять подтверждённые дефекты,
гипотезы и недостающие данные. Не предлагать готовые адреса/напряжения наугад.
Файлы — только для анализа, не прошивочный комплект.
''')
manifest={p.relative_to(OUT).as_posix():{'bytes':p.stat().st_size,'sha256':sha(p.read_bytes()),**({'provenance':sources[p.relative_to(OUT).as_posix()]['source']} if p.relative_to(OUT).as_posix() in sources else {})} for p in OUT.rglob('*') if p.is_file()}
write('SHA256-MANIFEST.json',json.dumps(manifest,indent=2,ensure_ascii=False))
archive=OUT.with_suffix('.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for p in sorted(OUT.rglob('*')):
  if p.is_file():z.write(p,OUT.name+'/'+p.relative_to(OUT).as_posix())
with zipfile.ZipFile(archive) as z:assert z.testzip() is None
print(json.dumps(dict(folder=str(OUT),archive=str(archive),files=len(manifest)+1,zip_bytes=archive.stat().st_size,sha256=sha(archive.read_bytes())),indent=2))
