#
# Operating System (WIN32, SOLARIS, LINUX, AIX, IRIX, OSX, FREEBSD)
#

OS = OSX

#
# Specify C compiler
#

CC = gcc

#
# Specify C++ compiler
#

CXX = g++

#
# Specify FORTRAN95 compiler and FORTRAN runtime library
#

FC = gfortran
FCLIB = -lgfortran

#
# Local body storage (yes/no)
#
# If 'no' all bodies all stored on all processors;
# this implies less communication during parallel balancing;
# When 'yes' bodies are only stored locally in parallel;
# this is more suitable for larger simulations;
#

LOCAL_BODIES = yes

# 
# Debug or optimized version switch (yes/no)
#

# general debug tests
DEBUG = yes
# gprof profiling data
PROFILE = no
# switch off memory pools
MEMDEBUG = no
# geometrical debug tests
GEOMDEBUG = no
# light parallel consitency tests
PARDEBUG = yes
# heavy parallel self-consitency tests
PSCTEST = no
# disable TRY/CATCH
NOTHROW = yes

#
# TIMERS (enable/disable detailed solver timings)
#

TIMERS = yes

#
# POSIX
#

POSIX = yes

#
# HDF5
#

HDF5 = yes

HDF5INC = -I/opt/local/include
HDF5LIB = -L/opt/local/lib -lhdf5 -lhdf5_hl

#
# XDR (must be set when HDF5 = no)
#

XDR = no

XDRINC = 
XDRLIB = 

#
# BLAS
#

BLAS = -lblas

#
# LAPACK
#

LAPACK = -llapack

#
# Python
#

PYTHON = -I/opt/local/Library/Frameworks/Python.framework/Versions/2.7/include/python2.7
PYTHONLIB = -L/opt/local/lib -lpython2.7

#
# OpenGL (yes/no)
#

OPENGL = yes
GLINC = -Wno-deprecated-declarations
GLLIB = -framework GLUT -framework OpenGL

#
# VBO  (OPENGL = yes)
#

VBO = yes

#
# OpenMP (yes/no) XXX (Under development) XXX
#

OPENMP = yes

#
# MPI (yes/no)
#

MPI = yes
MPICC = mpicc -Wno-unused-but-set-variable -fopenmp

#
# Zoltan load balancer (MPI = yes); optional
#

ZOLTAN = no
ZOLTANINC = -I/usr/local/include
ZOLTANLIB = -L/usr/local/lib -lzoltan

#
# Dynlb load balancer (MPI = yes) available at:
# https://github.com/tkoziara/dynlb
# This option is used when ZOLTAN = no above;
# Use path to dynlb directory below;
# Use -fopenmp flag with MPICC when compiling with dynlb;
#

DYNLB = ../dynlb

#
# PARMEC library available at:
# https://github.com/tkoziara/parmec
# This option is used to enable the HYBRID_SOLVER;
# If not required can be left as empty;
#

PARMEC = ../parmec

#
# Siconos (yes/no)
#

SICONOS = no
SICONOSINC = -I/usr/local/include/Siconos/Numerics
SICONOSLIB = -L/usr/local/lib -l SiconosNumerics
