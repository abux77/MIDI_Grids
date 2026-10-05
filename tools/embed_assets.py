"""Regenerate C assets from the preserved v0.6 maps and factory WAVs."""
import ast
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
tree = ast.parse((ROOT/'data/grids_data.py').read_text())
nodes = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id=='NODES' for t in n.targets))
assert len(nodes)==25 and all(len(n)==96 for n in nodes)
(ROOT/'src/grids_data.h').write_text('// Copyright Emilie Gillet / Mutable Instruments. GPL-3.0-or-later.\nstatic const unsigned char nodes[25][96] = {\n'+',\n'.join('{'+','.join(map(str,n))+'}' for n in nodes)+'\n};\n')
header='#pragma once\n#include <stdint.h>\n'
for name in ('BD01','SD01','HH01'):
    data=(ROOT/'samples'/f'{name}.WAV').read_bytes()
    header+=f'static const uint8_t factory_{name}[] = {{'+','.join(map(str,data))+'};\n'
header+='typedef struct {const char *name; const uint8_t *data; unsigned size;} factory_t;\nstatic const factory_t factory[] = {\n'+',\n'.join(f'{{"{n}.WAV",factory_{n},sizeof(factory_{n})}}' for n in ('BD01','SD01','HH01'))+'};\n'
(ROOT/'src/factory.h').write_text(header)
