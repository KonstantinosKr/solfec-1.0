# read time histories
def ro1_read_histories(sol, bod, path_string):
  from math import asin
  import pickle
  p0 = (0.005, 0.05, 0.035)
  p1 = (0.005, 0.05, 0.065)
  p2 = (0.005, 0.05, 0.04)
  p3 = (0.005, 0.05, 0.06)
  p4 = (0.03, 0.05, 0.065)
  p5 = (-0.07, 0.05, 0.065)
  dur = DURATION (sol)
  th = HISTORY (sol, [(bod, 'KINETIC'), (bod, 'INTERNAL'),
       (bod, p0, 'CX'), (bod, p0, 'CY'), (bod, p0, 'CZ'),
       (bod, p1, 'CX'), (bod, p1, 'CY'), (bod, p1, 'CZ'),
       (bod, p2, 'CX'), (bod, p2, 'CY'), (bod, p2, 'CZ'),
       (bod, p3, 'CX'), (bod, p3, 'CY'), (bod, p3, 'CZ'),
       (bod, p4, 'CX'), (bod, p4, 'CY'), (bod, p4, 'CZ'),
       (bod, p5, 'CX'), (bod, p5, 'CY'), (bod, p5, 'CZ'),
       (bod, p0, 'MISES'), (bod, p2, 'MISES')], dur[0], dur[1])
  odm = []
  for p0x, p0y, p0z, p1x, p1y, p1z in zip (th[3],th[4],th[5],th[6],th[7],th[8]):
    odm.append (((p1x-p0x)**2 + (p1y-p0y)**2 + (p1z-p0z)**2)**0.5)
  idm = []
  for p0x, p0y, p0z, p1x, p1y, p1z in zip (th[9],th[10],th[11],th[12],th[13],th[14]):
    idm.append (((p1x-p0x)**2 + (p1y-p0y)**2 + (p1z-p0z)**2)**0.5)
  rot = []
  for p0x, p0y, p0z, p1x, p1y, p1z in zip (th[15],th[16],th[17],th[18],th[19],th[20]):
    l = ((p1x-p0x)**2 + (p1y-p0y)**2 + (p1z-p0z)**2)**0.5
    a = ((p1x-p0x)/l, (p1y-p0y)/l, (p1z-p0z)/l)
    b = (-1, 0, 0)
    c = (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
    clen = (c[0]**2+c[1]**2+c[2]**2)**0.5
    rot.append(asin(clen))
  for i in range(0,len(th[21])): th[21][i] = 0.000001 * th[21][i] # Pa --> MPa
  for i in range(0,len(th[22])): th[22][i] = 0.000001 * th[22][i] # Pa --> MPa
 
  pickle.dump(th[0], open('out/reduced-order1/times.pickle', 'wb')) 
  pickle.dump(th[1], open('out/reduced-order1/kin-%s.pickle' % path_string, 'wb')) 
  pickle.dump(th[2], open('out/reduced-order1/int-%s.pickle' % path_string, 'wb')) 
  pickle.dump(odm, open('out/reduced-order1/odm-%s.pickle' % path_string, 'wb')) 
  pickle.dump(idm, open('out/reduced-order1/idm-%s.pickle' % path_string, 'wb')) 
  pickle.dump(rot, open('out/reduced-order1/rot-%s.pickle' % path_string, 'wb')) 
  pickle.dump(th[17], open('out/reduced-order1/p4z-%s.pickle' % path_string, 'wb')) 
  pickle.dump(th[20], open('out/reduced-order1/p5z-%s.pickle' % path_string, 'wb')) 
  pickle.dump(th[21], open('out/reduced-order1/p0mises-%s.pickle' % path_string, 'wb')) 
  pickle.dump(th[22], open('out/reduced-order1/p2mises-%s.pickle' % path_string, 'wb')) 
