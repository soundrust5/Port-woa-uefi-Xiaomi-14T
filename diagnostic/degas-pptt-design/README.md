# Independent Degas PPTT design artifacts

From the workspace root:

```
python diagnostic/degas-pptt-design/pptt_design.py
python diagnostic/degas-pptt-design/test_design.py
```

The generator emits a216-byte topology-only PPTT: one package and eight leaves,
without cache geometry. It never installs tables or modifies firmware. The test
executes the pinned original Windows package walker and offset validator on
every leaf for revisions2/3, and checks table invariants and CCIDX decoding.

The maintenance-parameter decoder deliberately reports that physical cache size
and sharing are not proven. No CPU register, SMC, MMIO or phone access occurs.
See `docs/DEGAS-PPTT-DESIGN-20261029.md` for the source audit and design limits.
