import sys

tests = [
         'tests/pinned-bar.py',
         'tests/math-pendulum.py',
         'tests/double-pendulum.py',
         'tests/projectile.py',
	 'tests/block-sliding.py',
	 'tests/arch.py',
	 'tests/BM01/BM01_pressure.py',
	 'tests/BM01/BM01_force.py',
	 'tests/BM02/BM02_hexa.py',
	 'tests/BM02/BM02_tetra.py',
	 'tests/BM03/BM03_hexa.py',
	 'tests/BM03/BM03_tetra.py',
	 'tests/BM04/BM04_hexa.py',
	 'tests/BM04/BM04_tetra.py',
	 'tests/BM06/BM06_hexa.py',
	 'tests/BM07/BM07_hexa.py',
	 ]

print '------------------------------------------------------------------------------------------'
print 'Solfec serial tests'
print '------------------------------------------------------------------------------------------'

for t in tests:
  print 'Running', t, '...', 
  sys.stdout.flush ()
  f = open (t)
  exec f
  f.close ()
  print '------------------------------------------------------------------------------------------'
