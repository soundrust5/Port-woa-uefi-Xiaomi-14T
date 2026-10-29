"""Render evidence-backed declared RAM/reserved ranges, without allocating RAM."""
import argparse
import json
from pathlib import Path


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('inventory',type=Path)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args()
    data=json.loads(args.inventory.read_text(encoding='utf-8'))
    memory=[r for r in data['reg_inventory'] if r['path']=='/memory' or r['path'].startswith('/memory@')]
    reserved=[r for r in data['reg_inventory'] if r['path'].startswith('/reserved-memory/')]
    dynamic=[p for p,v in data['nodes'].items() if p.startswith('/reserved-memory/') and 'size' in v and 'reg' not in v]
    lines=['# Объявленные диапазоны памяти Xiaomi 14T','',
           'Источник: живой FDT, полученный 2026-09-14. Это реестр DT, не готовая MemoryMapLib Mu и не список свободной памяти.',
           '',f"SHA-256 FDT: `{data['sha256']}`.",'',
           'Диапазоны используют исключительную верхнюю границу. Любая строка требует проверки владельца, жизненного цикла и доступа CPU. `no-map`/`reusable` — свойства DT, а не готовый UEFI memory type. Отсутствие `no-map` не даёт права перезаписывать регион.','']
    for title,rows in [('DRAM',memory),('Reserved-memory с reg',reserved)]:
        lines+=['## '+title,'','| Узел | Base | End exclusive | Size | no-map | reusable |','|---|---|---|---|---|---|']
        for r in rows:
            a=int(r['bus_address'],16);size=int(r['size'],16)
            if r['address_kind']!='identity_to_root':
                raise ValueError('Memory region needs bus translation')
            lines.append(f"| `{r['path']}` | `{a:#x}` | `{a+size:#x}` | `{size:#x}` | {r['no_map']} | {r['reusable']} |")
        lines.append('')
    lines+=['## Узлы size без статического reg','',
            'База не установлена этим свойством; сверять с runtime allocation, mblock и iomem. Не подставлять ноль.','']
    lines+=['- `'+p+'`' for p in dynamic]
    lines+=['','## Следующие проверки','',
            'Сопоставить все строки с iomem и mblock/ATAG, выяснить вложенные/пересекающиеся carveouts и dynamic allocation. Учесть secure firmware, загруженные образы, cache/MMU и DMA. Лишь затем выбрать FD, stack, heap и оформить UEFI attributes.',
            '', 'Текущие безопасные области для размещения нашего payload: **ещё не утверждены**.','']
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text('\n'.join(lines),encoding='utf-8')
    print(f'{len(memory)} DRAM ranges, {len(reserved)} reserved reg ranges, {len(dynamic)} dynamic nodes')


if __name__=='__main__':
    main()
