from shutil import copyfile
###
import os, sys
def where(program):
  for path in os.environ["PATH"].split(os.pathsep):
    if os.path.exists(os.path.join(path, program)):
      return path
  return None
path = where('parmec4')
if path == None:
  print 'ERROR: parmec4 not found in PATH!'
  print '       Download and compile parmec;'
  print '       Add parmec directory to PATH variable.'
  sys.exit(1)
sys.path.append(os.path.join (path, 'python'))
from acc_sweep import *
###

dO0 = 0.2
d01 = 0.2
d12 = 0.05
d47 = 0.01
d34 = (d12-d47)/2.
d45 = 0.02
dOz = 0.5
gap = 0.001
lwfact = 1.0 # TODO: make into ARGV
step = 1E-3
stop = 5.0
lofq = 3.0
hifq = 3.0
amag = 5.0

nodes0 = [dO0, 0, 0,
          dO0+d01, 0, 0,
          dO0+d01, d12, 0,
	  dO0, d12, 0,
	  dO0, d12-d34, 0,
	  dO0+d45, d12-d34, 0,
	  dO0+d45, d12-d34-d47, 0,
	  dO0, d12-d34-d47, 0,
          dO0, 0, dOz,
          dO0+d01, 0, dOz,
	  dO0+d01, d12, dOz,
	  dO0, d12, dOz,
	  dO0, d12-d34, dOz,
	  dO0+d45, d12-d34, dOz,
	  dO0+d45, d12-d34-d47, dOz,
	  dO0, d12-d34-d47, dOz]


elements0 = [8, 0, 1, 6, 7, 8, 9, 14, 15, 0,
             8, 6, 1, 2, 5, 14, 9, 10, 13, 0,
	     8, 4, 5, 2, 3, 12, 13, 10, 11, 0]


mesh0 = MESH (nodes0, elements0, 0)

nodes1 = [0, d34+gap, 0,
          dO0+d45-gap, d34+gap, 0,
	  dO0+d45-gap, d34+d47-gap, 0,
	  0, d34+d47-gap, 0,
          0, d34+gap, dOz,
          dO0+d45-gap, d34+gap, dOz,
	  dO0+d45-gap, d34+d47-gap, dOz,
	  0, d34+d47-gap, dOz]

mesh1 = HEX(nodes1, 1, 1, 2, 0, [0]*6)

sol = SOLFEC ('DYNAMIC', step, 'out/hs4-hybrid-lwfact-%g' % lwfact)

mat = BULK_MATERIAL (sol, model = 'KIRCHHOFF',
    young = 1E9, poisson = 0.25, density = 1E3)

bod0 = BODY (sol, 'FINITE_ELEMENT', mesh0, mat)
bod0.scheme = 'DEF_LIM'
bod0.damping = step

bod1 = BODY (sol, 'RIGID', mesh1, mat)

FIX_DIRECTION (bod0, tuple(nodes0[1*3:1*3+3]), (1, 0, 0))
FIX_DIRECTION (bod0, tuple(nodes0[1*3:1*3+3]), (0, 0, 1))
FIX_DIRECTION (bod0, tuple(nodes0[9*3:9*3+3]), (1, 0, 0))
FIX_DIRECTION (bod0, tuple(nodes0[9*3:9*3+3]), (0, 0, 1))

(vt, vd, vv, va) = acc_sweep (step, stop, lofq, hifq, amag)
tsv = [None]*(len(vt)+len(vd))
tsv[::2] = vt
tsv[1::2] = vv
tsv = TIME_SERIES (tsv)

SET_VELOCITY (bod0, tuple(nodes0[1*3:1*3+3]), (0, 1, 0), tsv)
SET_VELOCITY (bod0, tuple(nodes0[9*3:9*3+3]), (0, 1, 0), tsv)

ns = NEWTON_SOLVER ()

# parmec's output files are written to the same location as the input path
# for that to be the solfec's output directory, we copy parmec's input file there
copyfile('inp/devel/hybrid-solver4/hs4-parmec.py', sol.outpath+'/hs4-parmec.py')

# nubering of bodies in Parmec starts from 0 while in Solfec from 1
# hence below we used dictionary {0 : 1} as the parmec2solfec mapping
hs = HYBRID_SOLVER (sol.outpath+'/hs4-parmec.py', lwfact*0.005, {0:bod1.id}, ns,
                    ['-leeway', '%s'%(lwfact*gap)])

# set PARMEC output interval
hs.parmec_interval = 0.01;

import solfec as solfec # we need to be specific when using the OUTPUT command
solfec.OUTPUT (sol, 0.01) # since 'OUTPUT' in Solfec collides with 'OUTPUT' in Parmec

RUN (sol, hs, stop)

if sol.mode == 'READ' and not VIEWER():
  try:
    import matplotlib.pyplot as plt
    dur = DURATION (sol)
    th = solfec.HISTORY (sol, [(bod0, tuple(nodes0[0:3]), 'DY')], dur [0], dur [1])
    plt.plot (th[0], th[1], label='DY(node 0)')
    plt.xlabel ('Time [s]')
    plt.ylabel ('DY of node 0[m]')
    plt.legend(loc = 'upper right')
    plt.savefig (sol.outpath+'/node0dy.png')
  except ImportError:
    pass # no reaction
