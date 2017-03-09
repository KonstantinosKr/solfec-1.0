/* HYBRID_SOLVER implementation */

/*
The MIT License (MIT)

Copyright (c) 2016 EDF Energy

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/* Contributors: Tomasz Koziara */

#include <complex.h>
#include <stdlib.h>
#include <float.h>
#include "solfec.h"
#include "hys.hpp" /* :) */
#include "hys.h"
#include "sol.h"
#include "dom.h"
#include "alg.h"
#include "err.h"

#if MPI
#include <mpi.h>
#endif

#if MPI
/* initialize boundary */
static void init_boundary (HYBRID_SOLVER *hs, DOM *dom)
{
  int rank, size, *sendbuf, sendcount, *recvbuf, recvsize, *recvcounts, *displs, *ptr;
  MAP *found, *item;

  for (found = NULL, item = MAP_First(hs->solfec2parmec); item; item = MAP_Next (item))
  {
    BODY *bod = MAP_Find (dom->idb, item->key, NULL);

    if (bod)
    {
      MAP_Insert (NULL, &found, item->key, NULL, NULL);

      if (!bod->parmec) ERRMEM (bod->parmec = MEM_CALLOC (sizeof(PARMEC_FORCE))); /* allocate parmec force */
    }
  }

  MPI_Comm_rank (MPI_COMM_WORLD, &rank);
  MPI_Comm_size (MPI_COMM_WORLD, &size);

  sendcount = MAP_Size(found);
  recvsize = MAP_Size(hs->solfec2parmec) * size;
  ERRMEM (sendbuf = malloc (sendcount * sizeof(int)));
  if (rank == 0)
  {
    ERRMEM (recvbuf = malloc (recvsize * sizeof(int)));
    ERRMEM (recvcounts = MEM_CALLOC(size * sizeof(int)));
    ERRMEM (displs = MEM_CALLOC(size * sizeof(int)));
  }

  for (item = MAP_First(found), ptr = sendbuf; item; item = MAP_Next(item), ptr ++)
  {
    ptr[0] = (int) (long) item->key;
  }

  MPI_Gatherv (sendbuf, sendcount, MPI_INT, recvbuf, recvcounts, displs, MPI_INT, 0, MPI_COMM_WORLD);

  if (rank == 0)
  {
    MAP_Free (NULL, &found);

    for (int i = 0; i < size; i ++)
    {
      for (int j = 0; j < recvcounts[i]; j ++)
      {
	MAP_Insert (NULL, &found, (void*) (long) recvbuf[displs[i]+j], NULL, NULL);
      }
    }

    for (MAP *item = MAP_First(hs->solfec2parmec); item; item = MAP_Next (item)) /* make rank-wide consistency check */
    {
      BODY *bod = MAP_Find (found, item->key, NULL);
      ASSERT_TEXT (bod, "ERROR: Solfec-Parmec boundary body with Solfec id = %d has not been found", (int)(long) item->key);
      ASSERT_TEXT (bod->kind == RIG, "ERROR: Solfec-Parmec boundary body with Solfec id = %d is not rigid", (int)(long) item->key);
    }

    free (recvbuf);
    free (recvcounts);
    free (displs);
  }

  free (sendbuf);
  MAP_Free (NULL, &found);
}

/* apply constant boundary force from Solfec;
 * perform a number of Parmec time integration steps;
 * read back average velocity of boundary bodies into Solfec */
static void parmec_steps (HYBRID_SOLVER *hs, DOM *dom, double step, int nstep)
{
  MPI_Datatype subtypes[6] = {MPI_INT, MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE}, ift_type, alftrp_type;
  struct alftrp_struct { double a[3]; double l[3]; double f[3]; double t[3]; double r[9]; double p[3]; } *alftrp;
  struct ift_struct { int i[2]; double f[3]; double t[3]; } *ift;
  int blocklens[6] = {2, 3, 3, 3, 9, 3}, ift_size, ift_count, i, j;
  int rank, size, *ift_counts, *ift_displs;
  struct ift_struct *ift_all;
  struct alftrp_struct *alftrp_all;
  MPI_Aint displs[6];
  MAP *item;

  ift_size = MAP_Size (hs->solfec2parmec);
  ERRMEM (ift = MEM_CALLOC (ift_size * sizeof(struct ift_struct)));
  ERRMEM (alftrp = MEM_CALLOC (ift_size * sizeof(struct alftrp_struct)));

  MPI_Get_address (&ift->i, &displs[0]);
  MPI_Get_address (&ift->f, &displs[1]);
  MPI_Get_address (&ift->t, &displs[2]);
  displs[2] = displs[2] - displs[0];
  displs[1] = displs[1] - displs[0];
  displs[0] = 0;

  MPI_Type_create_struct (3, blocklens, displs, subtypes, &ift_type);
  MPI_Type_commit (&ift_type);

  subtypes[0] = MPI_DOUBLE;
  blocklens[0] = 3;
  MPI_Get_address (&alftrp->a, &displs[0]);
  MPI_Get_address (&alftrp->l, &displs[1]);
  MPI_Get_address (&alftrp->f, &displs[2]);
  MPI_Get_address (&alftrp->t, &displs[3]);
  MPI_Get_address (&alftrp->r, &displs[4]);
  MPI_Get_address (&alftrp->p, &displs[5]);
  displs[5] = displs[5] - displs[0];
  displs[4] = displs[4] - displs[0];
  displs[3] = displs[3] - displs[0];
  displs[2] = displs[2] - displs[0];
  displs[1] = displs[1] - displs[0];
  displs[0] = 0;

  MPI_Type_create_struct (6, blocklens, displs, subtypes, &alftrp_type);
  MPI_Type_commit (&alftrp_type);

  for (item = MAP_First(hs->solfec2parmec), j = 0; item; item = MAP_Next (item))
  {
    BODY *bod = MAP_Find (dom->idb, item->key, NULL);
    if (bod)
    {
      BODY_Rigid_Force (bod, dom->time, dom->step, ift[j].f, ift[j].t);
      ift[j].i[0] = bod->id;
      ift[j].i[1] = (int) (long) item->data; /* parmec particle number */
      j ++;
    }
  }
  ift_count = j;

  MPI_Comm_rank (MPI_COMM_WORLD, &rank);
  MPI_Comm_size (MPI_COMM_WORLD, &size);

  if (rank == 0)
  {
    ERRMEM (ift_all = malloc (size * ift_size * sizeof(struct ift_struct)));
    ERRMEM (alftrp_all = malloc (size * ift_size * sizeof(struct alftrp_struct)));
    ERRMEM (ift_counts = MEM_CALLOC(size * sizeof(int)));
    ERRMEM (ift_displs = MEM_CALLOC(size * sizeof(int)));
  }

  MPI_Gatherv (ift, ift_count, ift_type, ift_all, ift_counts, ift_displs, ift_type, 0, MPI_COMM_WORLD);

  if (rank == 0)
  {
    for (i = 0; i < size; i ++)
    {
      for (j = 0; j < ift_counts[i]; j ++)
      {
	struct ift_struct *item = &ift_all[ift_displs[i]+j];
	struct alftrp_struct *jtem = &alftrp_all[ift_displs[i]+j];
	parmec_set_force_and_torque (item->i[1], item->f, item->t);
	SET (jtem->a, 0.0); /* clear for velocities */
	SET (jtem->l, 0.0);
      }
    }

    for (int i = 0; i < nstep; i ++) /* parmec steps */
    {
      parmec_one_step (step, hs->parmec_interval, hs->parmec_prefix);

      for (i = 0; i < size; i ++)
      {
	for (j = 0; j < ift_counts[i]; j ++)
	{
	  double angular[3], linear[3];
	  struct ift_struct *item = &ift_all[ift_displs[i]+j];
	  struct alftrp_struct *jtem = &alftrp_all[ift_displs[i]+j];
	  parmec_get_angular_and_linear (item->i[1], angular, linear);
	  ACC (angular, jtem->a); /* accumulate velocities */
	  ACC (linear, jtem->l);
	}
      }
    }

    double inv = 1.0/(double)nstep;

    for (i = 0; i < size; i ++)
    {
      for (j = 0; j < ift_counts[i]; j ++)
      {
	struct ift_struct *item = &ift_all[ift_displs[i]+j];
	struct alftrp_struct *jtem = &alftrp_all[ift_displs[i]+j];
	SCALE (jtem->a, inv); /* average velocities */
	SCALE (jtem->l, inv);
        parmec_get_force_and_torque (item->i[1], nstep, jtem->f, jtem->t);
        parmec_get_rotation_and_position (item->i[1], jtem->r, jtem->p);
      }
    }
  }

  /* scatter parmec average velocities, force, torque, rotation and position */
  MPI_Scatterv (alftrp_all, ift_counts, ift_displs, alftrp_type, alftrp, ift_count, alftrp_type, 0, MPI_COMM_WORLD);

  for (j = 0; j < ift_count; j ++)
  {
    BODY *bod = MAP_Find (dom->idb, (void*) (long) ift[j].i[0], NULL);
    ASSERT_TEXT (bod && bod->parmec, "Inconsistent body identifier when transferring boundary data");
    COPY (alftrp[j].a, bod->velo);
    COPY (alftrp[j].l, bod->velo+3);
    COPY (alftrp[j].f, bod->parmec->force);
    COPY (alftrp[j].t, bod->parmec->torque);
    NNCOPY (alftrp[j].r, bod->conf);
    COPY (alftrp[j].p, bod->conf+9);
    SHAPE_Update (bod->shape, bod, (MOTION)BODY_Cur_Point);
  }

  MPI_Type_free (&ift_type);
  MPI_Type_free (&alftrp_type);

  if (rank == 0)
  {
    free (ift_all);
    free (alftrp_all);
    free (ift_counts);
    free (ift_displs);
  }

  free (ift);
  free (alftrp);
}
#else
/* initialize boundary */
static void init_boundary (HYBRID_SOLVER *hs, DOM *dom)
{
  for (MAP *item = MAP_First(hs->solfec2parmec); item; item = MAP_Next (item))
  {
    BODY *bod = MAP_Find (dom->idb, item->key, NULL);
    ASSERT_TEXT (bod, "ERROR: Solfec-Parmec boundary body with Solfec id = %d has not been found", (int)(long) item->key);
    ASSERT_TEXT (bod->kind == RIG, "ERROR: Solfec-Parmec boundary body with Solfec id = %d is not rigid", (int)(long) item->key);
    if (!bod->parmec) ERRMEM (bod->parmec = MEM_CALLOC (sizeof(PARMEC_FORCE)));
  }
}

/* apply constant boundary force from Solfec;
 * perform a number of Parmec time integration steps;
 * read back average velocity of boundary bodies into Solfec */
static void parmec_steps (HYBRID_SOLVER *hs, DOM *dom, double step, int nstep)
{
  MAP *item;

  for (item = MAP_First(hs->solfec2parmec); item; item = MAP_Next (item))
  {
    double force[3], torque[3];
    BODY *bod = MAP_Find (dom->idb, item->key, NULL);
    BODY_Rigid_Force (bod, dom->time, dom->step, force, torque);
    int num = (int) (long) item->data; /* parmec particle number */
    parmec_set_force_and_torque (num, force, torque);
    SET6 (bod->velo, 0.0);
  }

  for (int i = 0; i < nstep; i ++)
  {
    parmec_one_step (step, hs->parmec_interval, hs->parmec_prefix);

    for (item = MAP_First(hs->solfec2parmec); item; item = MAP_Next (item))
    {
      BODY *bod = MAP_Find (dom->idb, item->key, NULL);
      int num = (int) (long) item->data; /* parmec particle number */
      double angular[3], linear[3];
      parmec_get_angular_and_linear (num, angular, linear);
      ACC (angular, bod->velo);
      ACC (linear, bod->velo+3);
    }
  }

  double inv = 1.0/(double)nstep;

  for (item = MAP_First(hs->solfec2parmec); item; item = MAP_Next (item))
  {
    BODY *bod = MAP_Find (dom->idb, item->key, NULL);
    int num = (int) (long) item->data; /* parmec particle number */
    parmec_get_force_and_torque (num, nstep, bod->parmec->force, bod->parmec->torque);
    parmec_get_rotation_and_position (num, bod->conf, bod->conf+9);
    SHAPE_Update (bod->shape, bod, (MOTION)BODY_Cur_Point);
    SCALE6 (bod->velo, inv);
  }
}
#endif

/* create solver */
HYBRID_SOLVER* HYBRID_SOLVER_Create (char *parmec_file, double parmec_step, double parmec_interval[2],
                 char *parmec_prefix, MAP *parmec2solfec, void *solfec_solver, int solfec_solver_kind)
{
  HYBRID_SOLVER *hs;

  ERRMEM (hs = MEM_CALLOC (sizeof (HYBRID_SOLVER)));
  hs->parmec_file = parmec_file;
  hs->parmec_step = parmec_step;
  if (parmec_interval)
  {
    ERRMEM (hs->parmec_interval = malloc (2 * sizeof (double)));
    hs->parmec_interval[0] = parmec_interval[0];
    hs->parmec_interval[1] = parmec_interval[1];
  }
  else hs->parmec_interval = NULL;
  hs->parmec_prefix = parmec_prefix;
  hs->parmec2solfec = parmec2solfec;
  hs->solfec2parmec = NULL;
  for (MAP *item = MAP_First(parmec2solfec); item; item = MAP_Next (item))
  {
    MAP_Insert (NULL, &hs->solfec2parmec, item->data, item->key, NULL);
  }
  hs->solfec_solver = solfec_solver;
  hs->solfec_solver_kind = solfec_solver_kind;

#if MPI
  int rank;
  MPI_Comm_rank (MPI_COMM_WORLD, &rank);
  if (rank == 0) /* parmec lib is used on rank 0 */
#endif
  parmec_init(hs->parmec_file);

  /* TODO: perhaps invoke Solfec's python init so that Parmec's commands
           do not overwrite the Solfec's ones */

  return hs;
}

/* run solver */
void HYBRID_SOLVER_Run (HYBRID_SOLVER *hs, SOLFEC *sol, double duration)
{
#if HDF5
  if (CONTINUE_WRITE_FLAG())
  {
    fprintf (stderr, "ERROR: continued analysis [-c] is disabled with HYBRID_SOLVER\n");
    exit (1);

    /* TODO: see area in SOLFEC_Run for enabling this (use of 'duration' there and here);
             this also would need to be enabled on the parmec library level */
  }
#endif

  /* find Parmec time step */
  double actual_parmec_step = 0.5*sol->dom->step;
  int num_parmec_steps = 2;
  while (actual_parmec_step > hs->parmec_step)
  {
    actual_parmec_step *= 0.5;
    num_parmec_steps *= 2;
  }

  /* exclude contact detection between mapped Solfec bodies (boundary);
   * we do want contact detection between boundary and interior bodies */
  for (MAP *item = MAP_First (hs->solfec2parmec); item; item = MAP_Next (item))
  {
    for (MAP *jtem = MAP_First (hs->solfec2parmec); jtem; jtem = MAP_Next (jtem))
    {
      if (item->key != jtem->key)
      {
	AABB_Exclude_Body_Pair (sol->aabb, (int)(long)item->key, (int)(long)jtem->key);
      }
    }
  }

  
  if (sol->dom->time == 0.0)
  {
    /* initialize boundary */
    init_boundary (hs, sol->dom);

    /* initial half-step */
    parmec_steps (hs, sol->dom, actual_parmec_step, num_parmec_steps/2);
  }

  double time0 = sol->dom->time;

  /* integrate over duration */
  sol->t0 = sol->dom->time; /* these can be overwritten when negative duration is used */
  sol->duration = duration; /* in SOLFEC_Run, allowing for a correct elapsed time estimate */
  while (sol->dom->time < time0 + duration)
  {
#if 0
    for (MAP *item = MAP_First(hs->solfec2parmec); item; item = MAP_Next (item))
    {
      BODY *bod = MAP_Find (sol->dom->idb, item->key, NULL);
      int num = (int) (long) item->data; /* parmec particle number */
      parmec_get_rotation_and_position (num, bod->conf, bod->conf+9);
      SHAPE_Update (bod->shape, bod, (MOTION)BODY_Cur_Point);
    }
#endif

    SOLFEC_Run (sol, hs->solfec_solver_kind, hs->solfec_solver, -sol->dom->step); /* negative duration used */

    parmec_steps (hs, sol->dom, actual_parmec_step, num_parmec_steps);
  }

  /* include contact detection between mapped Solfec bodies;
   * this may be useful in case another solver is used following this one */
  for (MAP *item = MAP_First (hs->solfec2parmec); item; item = MAP_Next (item))
  {
    for (MAP *jtem = MAP_First (hs->solfec2parmec); jtem; jtem = MAP_Next (jtem))
    {
      if (item->key != jtem->key)
      {
	AABB_Include_Body_Pair (sol->aabb, (int)(long)item->key, (int)(long)jtem->key);
      }
    }
  }
}

/* destroy solver */
void HYBRID_SOLVER_Destroy (HYBRID_SOLVER *hs)
{
  if (hs->parmec_interval) free (hs->parmec_interval);
  MAP_Free (NULL, &hs->parmec2solfec);
  MAP_Free (NULL, &hs->solfec2parmec);
}
