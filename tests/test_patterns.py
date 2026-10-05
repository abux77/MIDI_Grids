"""Compare the compiled C engine against the preserved v0.6 mathematical rules."""
import ast, ctypes, random, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
# Read the checked-in C node bytes, so no external source path is required to run.
node_text = (ROOT/'src/grids_data.h').read_text().split('= {',1)[1]
NODES = [bytes(map(int, row.split(','))) for row in __import__('re').findall(r'\{([\d,]+)\}', node_text)]
tree=ast.parse((ROOT/'data/grids_data.py').read_text())
original=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='NODES' for t in n.targets))
assert tuple(NODES)==original, 'Generated map bytes differ from v0.6'
MAP = ((10,8,0,9,11),(15,7,13,12,6),(18,14,4,5,3),(23,16,21,1,2),(24,19,17,20,22))
def level(step,part,x,y):
    i,j=x>>6,y>>6
    fx,fy=(x<<2)&255,(y<<2)&255
    o=part*32+step
    a,b,c,d=(NODES[MAP[u][v]][o] for u,v in ((i,j),(i+1,j),(i,j+1),(i+1,j+1)))
    mix=lambda a,b,f: a+(((b-a)*f)>>8)
    return mix(mix(a,b,fx),mix(c,d,fx),fy)
class Engine(ctypes.Structure):
    _fields_=[('step',ctypes.c_uint8),('x',ctypes.c_uint8),('y',ctypes.c_uint8),('density',ctypes.c_uint8*3),('chaos',ctypes.c_uint8),('perturbation',ctypes.c_uint8*3),('rng',ctypes.c_uint32)]
with tempfile.TemporaryDirectory() as tmp:
    libpath=Path(tmp)/'grids.so'
    subprocess.run(['cc','-shared','-fPIC','-std=c11','-I'+str(ROOT/'include'),str(ROOT/'src/grids.c'),'-o',str(libpath)],check=True)
    lib=ctypes.CDLL(str(libpath));lib.grids_level.restype=ctypes.c_uint8
    lib.grids_step.argtypes=[ctypes.POINTER(Engine),ctypes.POINTER(ctypes.c_uint8)]
    lib.grids_step.restype=ctypes.c_uint8
    # Every X/Y pair, with changing instrument and pattern step.
    for x in range(256):
        for y in range(256):
            s=(x+y)&31;p=(x+y)%3
            assert lib.grids_level(s,p,x,y)==level(s,p,x,y)
    rng=random.Random(42)
    for case in range(1000):
        g=Engine();g.x=rng.randrange(256);g.y=rng.randrange(256);g.chaos=rng.randrange(256);g.rng=case+1
        g.density[:]=[rng.randrange(256) for _ in range(3)]
        for step in range(64):
            seed=g.rng;pert=list(g.perturbation)
            if step%32==0:
                for p in range(3):
                    seed=(seed*1664525+1013904223)&0xffffffff
                    pert[p]=((seed>>24)*(g.chaos>>2))>>8
            hits=acc=0
            for p in range(3):
                value=min(255,level(step%32,p,g.x,g.y)+pert[p])
                if value>255-g.density[p]:
                    hits|=1<<p
                    if value>192:acc|=1<<p
            accent=ctypes.c_uint8()
            assert lib.grids_step(ctypes.byref(g),ctypes.byref(accent))==hits
            assert accent.value==acc and g.step==(step+1)%32
    print('65,536 interpolation cases and 64,000 engine steps passed')
