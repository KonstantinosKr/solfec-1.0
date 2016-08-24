# two-body hybrid modeling example 
import matplotlib.pyplot as plt
import sys
import os
sys.path.append(os.path.dirname(__file__))
from acc_sweep import *

step = 1E-4  # time step
stop = 5.0   # duration of the simulation
damp = 1E-8  # amount of stiffness proportional damping
lofq = 5     # low frequency for the sweep
hifq = 15    # high frequency for the sweep
amag = 10.0  # acceleration magnitude
nbodl = 2    # number of bodies
nele = 2     # number of elements per body (along x, y, z)
nmod = 6     # number of linear modes to produce
l = 0.1      # length of one body
w = 0.1      # widhth of one body
h = 0.1      # height of one body
gap = 0.002  # gap
ostep = 1E-3 # output step
wavg = 0.01  # energy averaging time window [t-wavg/2, t+wavg/2]
fstop = 7.0  # end frequency for averaging velocities
             # (hand tunded in order to cover the pre-drop area)

GEOMETRIC_EPSILON (1E-9) # tiny geometrical tolerance (<< gap)

solfec = SOLFEC ('DYNAMIC', step, 'out/hybrid_modeling/two_cubes_simplified')
SURFACE_MATERIAL (solfec, model = 'SIGNORINI_COULOMB', friction = 0.05, restitution = 0.0)
bulk = BULK_MATERIAL (solfec, model = 'KIRCHHOFF', young = 1E9, poisson = 0.25, density = 1E3)

nodes = [0, 0, 0,
	 w, 0, 0,
	 w, l, 0,
	 0, l, 0,
	 0, 0, h,
	 w, 0, h,
	 w, l, h,
	 0, l, h]

bodies = [] # empty list for FEM bodies
for j in range (0, nbodl):
  shape = HEX (nodes, nele, nele, nele, 1, [1]*6)
  TRANSLATE (shape, (0, j*(l+gap), 0))
  body = BODY (solfec, 'RIGID', shape, bulk, form = 'BC')
  bodies.append (body) # append list

# moving obstacle
s1 = HEX (nodes, 1, 1, 1, 1, [1]*6)
TRANSLATE (s1, (0, -gap-l, 0))
s2 = HEX (nodes, 1, 1, 1, 1, [1]*6)
TRANSLATE (s2, (0, nbodl*(l+gap), 0))
body = BODY (solfec, 'OBSTACLE', [s1, s2], bulk)
FIX_DIRECTION (body, (w,-(gap+l),0), (1, 0, 0))
FIX_DIRECTION (body, (w,-(gap+l),0), (0, 0, 1))
FIX_DIRECTION (body, (w,-gap,0), (1, 0, 0))
FIX_DIRECTION (body, (w,-gap,h), (1, 0, 0))
FIX_DIRECTION (body, (w,-gap,0), (0, 0, 1))

# apply velocity constraint corresponding to the acceleration sin sweep
if VIEWER():
  data = acc_sweep (step, stop, lofq, hifq, amag, None, None, None)
else:
  data = acc_sweep (step, stop, lofq, hifq, amag,
                  'out/hybrid_modeling/acc.png',
		  'out/hybrid_modeling/vel.png',
		  'out/hybrid_modeling/dsp.png')
SET_VELOCITY (body, (w/2.,-(gap+l)/2.,h/2.), (0, 1, 0), TIME_SERIES (data))

# exclude contact between all bodies
for i in range (0,nbodl):
  CONTACT_EXCLUDE_BODIES (bodies[i], body)
  if i < nbodl-1:
    CONTACT_EXCLUDE_BODIES (bodies[i], bodies[i+1])

def spring_force (s,v):
  if s < -gap: return -1E6*s -1E3*v
  else: return 0.0

# apply spring constraints
p0 = (0.25*w, -gap, 0.25*h)
p1 = (0.25*w, 0, 0.25*h)
PUT_SPRING (body, p0, bodies[0], p1, spring_force, (-l,l), (0, 1, 0))
p0 = (0.75*w, -gap, 0.25*h)
p1 = (0.75*w, 0, 0.25*h)
PUT_SPRING (body, p0, bodies[0], p1, spring_force, (-l,l), (0, 1, 0))
p0 = (0.75*w, -gap, 0.75*h)
p1 = (0.75*w, 0, 0.75*h)
PUT_SPRING (body, p0, bodies[0], p1, spring_force, (-l,l), (0, 1, 0))
p0 = (0.25*w, -gap, 0.75*h)
p1 = (0.25*w, 0, 0.75*h)
PUT_SPRING (body, p0, bodies[0], p1, spring_force, (-l,l), (0, 1, 0))

p0 = (0.25*w, (nbodl-1)*(l+gap)+l, 0.25*h)
p1 = (0.25*w, nbodl*(l+gap), 0.25*h)
PUT_SPRING (bodies[nbodl-1], p0, body, p1, spring_force, (-l,l), (0, 1, 0))
p0 = (0.75*w, (nbodl-1)*(l+gap)+l, 0.25*h)
p1 = (0.75*w, nbodl*(l+gap), 0.25*h)
PUT_SPRING (bodies[nbodl-1], p0, body, p1, spring_force, (-l,l), (0, 1, 0))
p0 = (0.75*w, (nbodl-1)*(l+gap)+l, 0.75*h)
p1 = (0.75*w, nbodl*(l+gap), 0.75*h)
PUT_SPRING (bodies[nbodl-1], p0, body, p1, spring_force, (-l,l), (0, 1, 0))
p0 = (0.25*w, (nbodl-1)*(l+gap)+l, 0.75*h)
p1 = (0.25*w, nbodl*(l+gap), 0.75*h)
PUT_SPRING (bodies[nbodl-1], p0, body, p1, spring_force, (-l,l), (0, 1, 0))

for i in range (0, nbodl-1):
  p0 =  (0.25*w, i*(l+gap)+l, 0.25*h)
  p1 =  (0.25*w, (i+1)*(l+gap), 0.25*h)
  PUT_SPRING (bodies[i], p0, bodies[i+1], p1, spring_force, (-l,l), (0, 1, 0))
  p0 =  (0.75*w, i*(l+gap)+l, 0.25*h)
  p1 =  (0.75*w, (i+1)*(l+gap), 0.25*h)
  PUT_SPRING (bodies[i], p0, bodies[i+1], p1, spring_force, (-l,l), (0, 1, 0))
  p0 =  (0.75*w, i*(l+gap)+l, 0.75*h)
  p1 =  (0.75*w, (i+1)*(l+gap), 0.75*h)
  PUT_SPRING (bodies[i], p0, bodies[i+1], p1, spring_force, (-l,l), (0, 1, 0))
  p0 =  (0.25*w, i*(l+gap)+l, 0.75*h)
  p1 =  (0.25*w, (i+1)*(l+gap), 0.75*h)
  PUT_SPRING (bodies[i], p0, bodies[i+1], p1, spring_force, (-l,l), (0, 1, 0))

# enable gravity
# GRAVITY (solfec, (0, 0, -10))

# create constraints solver
slv = NEWTON_SOLVER ()

# output results every 'ostep'
OUTPUT (solfec, ostep)

# run simulation
RUN (solfec, slv, stop)

# post-process results
if not VIEWER() and solfec.mode == 'READ':
  iavg = 1 + int (wavg / ostep) / 2
  tstop = 0.0
  data = []
  for b in bodies:
    data.append ((b, 'KINETIC'))
    data.append ((b, 'INTERNAL'))
    data.append ((b, b.center, 'VY'))
  th = HISTORY (solfec, data, 0, stop)
  n = len (th[0])
  for k in range (0, nbodl):
    fq = []
    ek = []
    ei = []
    vy = []
    for i in range (0, n):
      if i >= iavg and i < n-iavg-1:
	vek = 0.0
	vei = 0.0
	vvy = 0.0
	for j in range (i-iavg, i+iavg+1):
	  vek += th [3*k+1][j]
	  vei += th [3*k+2][j]
	  vvy += abs(th [3*k+3][j])

        f = lofq + (hifq-lofq)*(th[0][i]/stop)
	if f > fstop: tstop = th[0][i]
	fq.append (f)
	ek.append (vek/(2.0*iavg+1.0))
	ei.append (vei/(2.0*iavg+1.0))
	vy.append (vvy/(2.0*iavg+1.0))
       
    plt.clf ()
    plt.plot (fq, ek, lw = 2, label = 'kinetic')
    plt.plot (fq, ei, lw = 2, label = 'internal')
    plt.xlim ((lofq, hifq))
    plt.legend(loc = 'best')
    plt.xlabel ('Frequency $(Hz)$')
    plt.ylabel ('Energy $(J)$')
    plt.savefig ('out/hybrid_modeling/two_cubes_simplified/ene'+str(k)+'.png')

    plt.clf ()
    plt.plot (fq, vy, lw = 2)
    plt.xlim ((lofq, hifq))
    plt.xlabel ('Frequency $(Hz)$')
    plt.ylabel ('Velocity vy $(m/s)$')
    plt.savefig ('out/hybrid_modeling/two_cubes_simplified/vy'+str(k)+'.png')

    # averge pre-drop-off velocity for body k
    vavg = 0.0
    nvavg = 0.0
    for (f, v) in zip(fq, vy):
      if f < fstop:
        vavg += v
	nvavg += 1.0

    print 'Average pre-drop-off velocity for body', k, 'is', vavg/nvavg
  
  # average input impact velocity
  vavg = 0.0
  nvavg = 0.0
  SEEK (solfec, 0.0)
  while solfec.time < tstop:
    for con in solfec.constraints:
      if con.kind == 'CONTACT':
        vavg += con.V[2]
	nvavg += 1.0
    FORWARD (solfec, 1)

  print 'Avererage impact input velocity:', vavg/nvavg
