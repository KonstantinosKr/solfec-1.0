from shutil import copyfile

M = 5 # must be same as in hs1-paremc.py
N = 3 # must be same as in hs1-paremc.py
gap = 0.001 # must be same as in hs1-paremc.py
step = 5E-4
stop = 5 # must be <= stop in hs1-parmec.py

sol = SOLFEC ('DYNAMIC', step, 'out/hybrid-solver1')

mat = BULK_MATERIAL (sol, model = 'KIRCHHOFF', young = 1E6, poisson = 0.25, density = 100)

SURFACE_MATERIAL (sol, model = 'SIGNORINI_COULOMB', friction = 0.1)

nodes = [0.0, 0.0, 0.0,
         0.1, 0.0, 0.0,
	 0.1, 0.1, 0.0,
	 0.0, 0.1, 0.0,
	 0.0, 0.0, 0.1,
	 0.1, 0.0, 0.1,
	 0.1, 0.1, 0.1,
	 0.0, 0.1, 0.1]

for i in [M-1, M+N]:
  msh = HEX (nodes, 1, 1, 1, 0, [0, 1, 2, 3, 4, 5])
  TRANSLATE (msh, (i*(0.1+gap), 0, 0))
  bod1 = BODY (sol, 'RIGID', msh, mat) # boundary bodies are rigid

for i in range(0,N):
  msh = HEX (nodes, 2, 2, 2, 0, [0, 1, 2, 3, 4, 5])
  TRANSLATE (msh, ((M+i)*(0.1+gap), 0, 0))
  p1 = msh.node(0)
  p2 = msh.node(2)
  p3 = msh.node(8)
  p4 = msh.node(18)
  bod2 = BODY (sol, 'FINITE_ELEMENT', msh, mat)
  bod2.scheme = 'DEF_LIM' # semi-implicit time integration
  bod2.damping = 1E-4 # damping out free vibrations
  FIX_DIRECTION (bod2, p1, (0, 0, 1))
  FIX_DIRECTION (bod2, p2, (0, 0, 1))
  FIX_DIRECTION (bod2, p3, (0, 0, 1))
  FIX_DIRECTION (bod2, p1, (0, 1, 0))
  FIX_DIRECTION (bod2, p2, (0, 1, 0))
  FIX_DIRECTION (bod2, p4, (0, 1, 0))

ns = NEWTON_SOLVER ()

# parmec's output files are written to the same location as the input path
# for that to be the solfec's output directory, we copy parmec's input file there
copyfile('examples/hybrid-solver1/hs1-parmec.py', 'out/hybrid-solver1/hs1-parmec.py')

# nubering of bodies in Parmec starts from 0 while in Solfec from 1
# hence below we used dictionary {0 : 1} as the parmec2solfec mapping
hs = HYBRID_SOLVER ('out/hybrid-solver1/hs1-parmec.py', 1E-4, {M-1:1, M:2}, ns)

# set PARMEC output interval
hs.parmec_interval = 0.03;

import solfec as solfec # we need to be specific when using the OUTPUT command
solfec.OUTPUT (sol, 0.03) # since 'OUTPUT' in Solfec collides with 'OUTPUT' in Parmec

RUN (sol, hs, stop)

if sol.mode == 'READ' and not VIEWER():
  XDMF_EXPORT (sol, (0.0, stop), 'out/hybrid-solver1/hs1-solfec', [3, 4, 5])
