/*
 * bod.h
 * Copyright (C) 2007, Tomasz Koziara (t.koziara AT gmail.com)
 * --------------------------------------------------------------
 * general body
 */

/* This file is part of Solfec.
 * Solfec is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Solfec is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Solfec. If not, see <http://www.gnu.org/licenses/>. */

#include <stdlib.h>
#include "pbf.h"
#include "shp.h"
#include "tms.h"
#include "set.h"
#include "mtx.h"
#include "mat.h"
#include "msh.h"
#include "cra.h"

#ifndef DOMAIN_TYPE
#define DOMAIN_TYPE
typedef struct domain DOM;
#endif

#ifndef SOLFEC_TYPE
#define SOLFEC_TYPE
typedef struct solfec SOLFEC;
#endif

#ifndef __bod__
#define __bod__

/* finite element formulation */
typedef enum {TOTAL_LAGRANGIAN = 1, BODY_COROTATIONAL,
  BODY_COROTATIONAL_MODAL, BODY_COROTATIONAL_REDUCED_ORDER} FEMFORM; /* must be > 1 (see BODY_Pack in bod.c) */ 

typedef struct general_force FORCE;
typedef struct parmec_force PARMEC_FORCE;
typedef struct display_point DISPLAY_POINT;
typedef void (*FORCE_FUNC) (void *data, void *call, /* user data and user callback pointers */
                            int nq, double *q, int nu, double *u,   /* user defined data, configuration, velocity, time, time step */
                            double t, double h, double *f);  /* for rigid bodies 'f' comprises [spatial force; spatial torque; referential torque];
                                                                for other types of bodies 'f' is the generalised force */
#ifndef BODY_TYPE
#define BODY_TYPE
typedef struct general_body BODY;
#endif

/* results
 * value kinds */
typedef enum
{
  VALUE_COORD,
  VALUE_DISPLACEMENT,
  VALUE_DISP_NORM,
  VALUE_VELOCITY,
  VALUE_VELO_NORM,
  VALUE_STRESS,
  VALUE_MISES,
  VALUE_STRESS_AND_MISES
} VALUE_KIND;

/* time integration schemes */
typedef enum 
{
  SCH_RIG_POS,   /* rigid: NEW1 with positive energy drift (high accuracy, approximate momentum conservation) */
  SCH_RIG_NEG,   /* rigid: NEW2 with with negative energy drift (exact momentum conservation) (DEFAULT) */
  SCH_RIG_IMP,   /* rigid: NEW3 semi-simplict and stable (no energy drift, extact momentum conservation) */
                 /* reference: T. Koziara, N. Bicanic. Simple and efficient integration of rigid rotations suitable for constraint solvers. IJNME, 81:1073-1092, 2009 */

  SCH_DEF_EXP,   /* deformable: explicit scheme (DEFAULT) */
                 /* reference: T. Koziara, PhD theis: Aspects of computational contact dynamics, University of Glasgow, 2008 */

  SCH_DEF_LIM   /* deformable: linearly implicit scheme */
                 /* reference: M. Zhang, R.D. Skeel. Cheap implicit symplectic integrators. Applied Numerical Mathematics, 6:297-302, 1997 */
} SCHEME;

struct general_force
{
  enum {SPATIAL   = 0x01,
        CONVECTED = 0x02,
        TORQUE    = 0x04, /* applies only to rigid bodies */
        PRESSURE  = 0x08} kind; /* force kind  */

  double ref_point [3],         /* referential point */
         direction [3];         /* spatial or referential */

  TMS *data;

  void *call;

  FORCE_FUNC func;

  int surfid; /* pressure surface id */

  FORCE *next;
};

struct parmec_force
{
  double force[3]; /* rigid body force */
  double torque[3]; /* spatial rigid body torque */
};

struct display_point /* auxiliary display point for verification purposes */
{
  double X [3], x [3];

  SGP *sgp;

  char *label;
};

/* energy kinds */
#define KINETIC  0
#define EXTERNAL 1
#define CONTWORK 2
#define FRICWORK 3
#define INTERNAL 4
#define BODY_ENERGY_SPACE 5
#define BODY_ENERGY_SIZE(kind) (kind == OBS ? 0 : kind == RIG ? 4 : 5)

/* body flags */
typedef enum
{
  BODY_DETECT_SELF_CONTACT = 0x0001, /* enable self contact detection */
  BODY_CHECK_FRACTURE      = 0x0002, /* enable fracture check for finite element bodies */
  BODY_PARENT              = 0x0010, /* a parent body */
  BODY_CHILD               = 0x0020, /* a child body */
  BODY_CHILD_UPDATED       = 0x0040, /* an updated child */
  BODY_ABSENT              = 0x0080  /* body whose state was not read */
} BODY_FLAGS;

/* flags that are migrated with bodies (the rest is filtered out) */
#define BODY_PERMANENT_FLAGS (BODY_DETECT_SELF_CONTACT|BODY_CHECK_FRACTURE)

struct general_body
{
  enum {OBS, RIG, PRB, FEM} kind; /* obstacle, rigid, pseudo-rigid, finite element */

  unsigned int id;  /* unique identifier (for serialization & parallel processing) */

  BULK_MATERIAL *mat; /* default material */

  double ref_mass,
         ref_volume,
         ref_center [3],
         ref_tensor [9]; /* RIG => Inertia tensor
			    PRB => Euler tensor */

  double *conf,    /* configuration */
         *velo;    /* velocity */

  int dofs;        /* number of velocity degrees of freedom */

  double *field;   /* FEM field variables at mesh nodes (# num nodes * mat->nfield) */

  SET *con;        /* adjacent constraints */

  FORCE *forces;   /* applied external forces */

  PARMEC_FORCE *parmec; /* parmec boundary force */
  
  CRACK *cra;     /* cracks */

  SHAPE *shape;    /* shape of the body */

  SGP *sgp;        /* shape and geometric object pairs */

  int nsgp;        /* number of them (above) */

  double extents [6];  /* shape extents */

  SCHEME scheme;    /* integration scheme */

  MX *inverse;      /* generalized inverse inertia oprator */

  MX *M;            /* inertia operator */

  MX *K;            /* stiffness operator */

  double damping;   /* stiffness proportional damping */

  double *eval;     /* eigenvalues */

  MX *evec;         /* eigenvectors */

  char *elabel; /* registered FE base label */

  DOM *dom;        /* domain storing the body */

  BODY *prev,       /* list */
       *next;

  char *label;      /* user specified label */

  BODY_FLAGS flags;      /* flags */

  FEMFORM form; /* FEM formulation */

  MESH *msh; /* background FEM mesh when shape is made of CONVEX objects */

  double energy [BODY_ENERGY_SPACE]; /* kinetic, external, contwork, fricwork, internal */

  double cristep0; /* critical time step at time 0 used by FE bodies */

  unsigned char fracture; /* fracture flag */

  int rank; /* parent => new/current rank; child => parent's rank */

#if MPI
  SET *children, *prevchildren; /* set of children ranks for a parent/set of other children ranks for a child; set of previous children ranks for a parent */
#else
  void *rendering; /* rendering data */

  SET *displaypoints; /* display points set */
#endif
};

/* body pointer cast */
#define BODY(bod) ((BODY*)(bod))

/* create a body */
BODY* BODY_Create (short kind, SHAPE *shp, BULK_MATERIAL *mat, char *label, BODY_FLAGS flags, short form, MESH *msh, MX *evec, double *eval, char *elabel);

/* get body kind string */
char* BODY_Kind (BODY *bod);

/* get configuration size */
int BODY_Conf_Size (BODY *bod);

/* overwrite mass and volume characteristics */
void BODY_Overwrite_Chars (BODY *bod, double mass, double volume, double *center, double *tensor);

/* overwrite body state */
void BODY_Overwrite_State (BODY *bod, double *q, double *u);

/* apply an initial rigid motion velocity */
void BODY_Initial_Velocity (BODY *bod, double *linear, double *angular);

/* set rigid motion */
void BODY_From_Rigid (BODY *bod, double *rotation, double *position, double *angular, double *linear);

/* apply a force (if 'func' is given, 'data' is regarded as the user data pointer to the callback 'func') */
void BODY_Apply_Force (BODY *bod, short kind, double *point, double *direction, TMS *data, void *call, FORCE_FUNC func, int surfid);

/* remove all forces */
void BODY_Clear_Forces (BODY *bod);

/* set new mapterial */
void BODY_Material (BODY *bod, int volume, BULK_MATERIAL *mat);

/* initialise dynamic time stepping */
void BODY_Dynamic_Init (BODY *bod);

/* estimate critical step for the dynamic scheme */
double BODY_Dynamic_Critical_Step (BODY *bod);

/* perform the initial half-step of the dynamic scheme */
void BODY_Dynamic_Step_Begin (BODY *bod, double time, double step);

/* perform the final half-step of the dynamic scheme */
void BODY_Dynamic_Step_End (BODY *bod, double time, double step);

/* initialise static time stepping */
void BODY_Static_Init (BODY *bod);

/* perform the initial half-step of the static scheme */
void BODY_Static_Step_Begin (BODY *bod, double time, double step);

/* perform the final half-step of the static scheme */
void BODY_Static_Step_End (BODY *bod, double time, double step);

/* update body extents */
void BODY_Update_Extents (BODY *bod);

/* motion x = x (X, state) */
void BODY_Cur_Point (BODY *bod, SGP *sgp, double *X, double *x);

/* inverse motion X = X (x, state) */
void BODY_Ref_Point (BODY *bod, SGP *sgp, double *x, double *X);

/* pull-forward v = {dx/dX} V (X, state) */
void BODY_Cur_Vector (BODY *bod, void *ele, double *X, double *V, double *v);

/* push-back V = {dX/dx} v (x, state) */
void BODY_Ref_Vector (BODY *bod, void *ele, double *x, double *v, double *V);

/* obtain spatial velocity at (sgp, referential point), expressed in the local spatial 'base' */
void BODY_Local_Velo (BODY *bod, SGP *sgp, double *point, double *base, double *prevel, double *curvel);

/* return transformation operator from the generalised to the local velocity space at (sgp, point, base) */
MX* BODY_Gen_To_Loc_Operator (BODY *bod, short constraint_kind, SGP *sgp, double *point, double *base);

/* compute current kinetic energy */
double BODY_Kinetic_Energy (BODY *bod);

/* get some values at a referential point */
void BODY_Point_Values (BODY *bod, double *point, VALUE_KIND kind, double *values);

/* split body by a referential plane; output one body with new boundary or two bodies if fragmentation occurs */
void BODY_Split (BODY *bod, double *point, double *normal, short topoadj, int surfid[2], BODY **one, BODY **two);

/* split MESH-based body by surface definned by inter-element mesh faces;
 * 'surf' defines faces as follows: [(4, n1, n2, n3, n4), (3, n1, n2, n3), ..., 0];
 * 'sid1' and 'sid2' are surface ids on the input and ouput bodies respectively;
 * 'label1' and 'label2' are optional new labels;
 * The index to value mapping in 'lst1' and 'lst2' defines the relationship between
 * the node index in the original MESH and the newly created MESH(s);
 * return arguments:
 * 'bod1', 'lst1', 'nlst1', 'bod2', 'lst2', 'nlst2' if 'bod' mesh was split in two pieces;
 * 'bod1', 'lst1', 'nlst1', NULL, NULL, 0 if 'bod' mesh was modified;
 *  NULL, NULL, 0, NULL, NULL, 0 if no modification happened or more then two fragements were created;
 * returned value: number of created fragments */
int BODY_Split_Mesh (BODY *bod, int *surf, int sid1, int sid2, char *label1, char *label2,
                     BODY **bod1, int **lst1, int *nlst1, BODY **bod2, int **lst2, int *nlst2);

/* separate body whose shape is separable into sub-bodies */
BODY** BODY_Separate (BODY *bod, int *m);

/* write body state */
void BODY_Write_State (BODY *bod, PBF *bf);

/* read body state */
void BODY_Read_State (BODY *bod, PBF *bf, int iover);

/* release body memory */
void BODY_Destroy (BODY *bod);

/* pack body into double and integer buffers (d and i buffers are of initial
 * dsize and isize, while the final numberof of doubles and ints is packed) */
void BODY_Pack (BODY *bod, int *dsize, double **d, int *doubles, int *isize, int **i, int *ints);

/* unpack body from double and integer buffers (unpacking starts at dpos and ipos in
 * d and i and no more than a specific number of doubles and ints can be red) */
BODY* BODY_Unpack (SOLFEC *sol, int *dpos, double *d, int doubles, int *ipos, int *i, int ints);

#if MPI
/* parent bodies store all body data and serve for time stepping */
void BODY_Parent_Pack (BODY *bod, int *dsize, double **d, int *doubles, int *isize, int **i, int *ints);
void BODY_Parent_Unpack (BODY *bod, int *dpos, double *d, int doubles, int *ipos, int *i, int ints);

/* child bodies store a minimal subset of needed data and serve for constraint solution */
void BODY_Child_Pack (BODY *bod, int *dsize, double **d, int *doubles, int *isize, int **i, int *ints);
void BODY_Child_Unpack (BODY *bod, int *dpos, double *d, int doubles, int *ipos, int *i, int ints);

/* child body updates pack and unpack configurations and update shapes */
void BODY_Child_Update_Pack (BODY *bod, int *dsize, double **d, int *doubles, int *isize, int **i, int *ints);
void BODY_Child_Update_Unpack (BODY *bod, int *dpos, double *d, int doubles, int *ipos, int *i, int ints);
#endif

/* compute c = alpha * INVERSE (bod) * b + beta * c */
void BODY_Invvec (double alpha, BODY *bod, double *b, double beta, double *c);

/* export MBFCP definition */
void BODY_2_MBFCP (BODY *bod, FILE *out);

/* caculate rigid body force and torque from applied point forces and constraints */
void BODY_Rigid_Force (BODY *bod, double time, double step, double *linforc, double *spatorq);

#endif
