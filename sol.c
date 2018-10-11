/*
 * sol.c
 * Copyright (C) 2008, Tomasz Koziara (t.koziara AT gmail.com)
 * --------------------------------------------------------------
 * solfec main module
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

#if POSIX
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <ftw.h>
#include <unistd.h>
#endif

#if MPI
#if ZOLTAN
#include <zoltan.h>
#endif
#include <time.h>
#include "put.h"
#endif

#include <string.h>
#include <limits.h>
#include <float.h>
#include "solfec.h"
#include "sol.h"
#include "dio.h"
#include "err.h"
#include "tmr.h"
#include "mrf.h"


#if POSIX
/* http://stackoverflow.com/questions/5467725/how-to-delete-a-directory-and-its-contents-in-posix-c */
int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
  int rv = remove(fpath);

  if (rv) perror(fpath);

  return rv;
}

int rmrf(char *path)
{
  return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}
#endif

/* defulat initial amoung of boxes */
#define DEFSIZE 1024

/* clean labeled timers (total member of TIMING) */
static void clean_timers (SOLFEC *sol)
{
  for (MAP *item = MAP_First (sol->timers); item; item = MAP_Next (item))
  {
    TIMING *t = item->data;
    t->total = 0.0;
  }
}

/* turn on verbosity */
static int verbose_on (SOLFEC *sol)
{
  if (sol->verbose > 0)
  {
    sol->dom->verbose = 1;
  }

  return sol->verbose > 0;
}

/* turn off verbosity */
static int verbose_off (SOLFEC *sol)
{
  sol->dom->verbose = 0;

  return 0;
}

/* copy output path */
static char* copyoutpath (char *outpath)
{
  char *out = NULL;
  int l, k, m;

  if ((l = outpath ? strlen (outpath) : 0))
  {
    m = 0;
    k = OUTPUT_SUBDIR() ? strlen (OUTPUT_SUBDIR()) : 0;
    if (k && outpath [l-1] != '/') m = 1;
    ERRMEM (out = malloc (l + k + m + 1));
    strcpy (out, outpath);
    if (k)
    {
      if (outpath [l-1] != '/') out [l] = '/';
      strcpy (&out [l+m], OUTPUT_SUBDIR());
    }
  }

  return out;
}

/* from directory path get last name */
static char *lastname (char *path)
{
  int l = strlen (path);

  while (l > 0 && path [l-1] != '/') l --;

  return &path [l];
}

/* get file path from directory path */
static char *getpath (char *outpath)
{
  int l = strlen (outpath),
      n = l + strlen (lastname (outpath)) + 8;
  char *path;

  ERRMEM (path = malloc (n));
  strcpy (path, outpath);
  path [l] = '/';
  strcpy (path+l+1, lastname (outpath));

  return path;
}

/* attempt reading output path */
static PBF* readoutpath (char *outpath)
{
  char *path = getpath (outpath);
  PBF *bf = PBF_Read (path);

  free (path);
  return bf;
}

/* attempt writing output path */
static PBF* writeoutpath (char *outpath, PBF_FLG append)
{
#if POSIX
  int i, l = strlen (outpath);

  for (i = 0; i < l; i ++) /* create all directories on the way */
  {
    if (outpath [i] == '/')
    {
       outpath [i] = '\0';
       mkdir (outpath, 0777); /* POSIX */
       outpath [i] = '/';
    }
  }
  mkdir (outpath, 0777); /* POSIX */
#endif

  char *path = getpath (outpath);
  PBF *bf = PBF_Write (path, append, PBF_ON);

  free (path);
  return bf;
}

/* copy a file */
static void copyfile (char *from, char *to)
{
  FILE *f, *t;
  char c;

  ASSERT (f = fopen (from, "rb"), ERR_FILE_OPEN);
  ASSERT (t = fopen (to, "wb"), ERR_FILE_OPEN);

  while (!feof(f))
  {
    c = fgetc(f);
    ASSERT (!ferror(f), ERR_FILE_READ);
    if(!feof(f)) fputc(c, t);
    ASSERT (!ferror(t), ERR_FILE_WRITE);
  }

  ASSERT (fclose (f) == 0, ERR_FILE_CLOSE);
  ASSERT (fclose (t) == 0, ERR_FILE_CLOSE);
}

/* output state */
static void write_state (SOLFEC *sol, void *solver, SOLVER_KIND kind)
{
#if HDF5
  /* open and append */
  if (!(sol->bf = writeoutpath (sol->outpath, PBF_ON))) THROW (ERR_FILE_OPEN);
#endif

  /* set compression */

  sol->bf->compression = sol->compression;

  /* write time */

  PBF_Time (sol->bf, &sol->dom->time); /* the only domain member written outside of it */

  /* write initial flags */

  if (sol->iover < 0)
  {
    sol->iover = -sol->iover; /* make positive */
    PBF_Label (sol->bf, "IOVER");
    PBF_Int (sol->bf, &sol->iover, 1);
  }

  /* write domain */

  DOM_Write_State (sol->dom, sol->bf);

  /* write timers */

#if 0 /* HDF5 */
  PBF_Push (sol->bf, "TIMERS");
  for (MAP *item = MAP_First (sol->timers); item; item = MAP_Next (item))
  {
    TIMING *t = item->data;
    PBF_Double2 (sol->bf, item->key, &t->total, 1);
  }
  PBF_Pop (sol->bf);
#else
  int numt = MAP_Size (sol->timers);
  PBF_Label (sol->bf, "TIMERS");
  PBF_Int (sol->bf, &numt, 1);
  
  for (MAP *item = MAP_First (sol->timers); item; item = MAP_Next (item))
  {
    TIMING *t = item->data;

    PBF_Label (sol->bf, item->key);
    PBF_Double (sol->bf, &t->total, 1);
    PBF_String (sol->bf, (char**) &item->key);
  }
#endif

  clean_timers (sol); /* restart total timing */

  /* write solver state */

  switch (kind)
  {
  case GAUSS_SEIDEL_SOLVER: GAUSS_SEIDEL_Write_State (solver, sol->bf); break;
  case PENALTY_SOLVER: PENALTY_Write_State (solver, sol->bf); break;
  case NEWTON_SOLVER: NEWTON_Write_State (solver, sol->bf); break;
#if WITHSICONOS
  case SICONOS_SOLVER: SICONOS_Write_State (solver, sol->bf); break;
#endif
  case TEST_SOLVER: TEST_Write_State (solver, sol->bf); break;
  default: break;
  }

#if HDF5
  /* close to ensure flushed buffers */
  PBF_Close (sol->bf);
  sol->bf = NULL;
#endif
}

/* read timers alone */
static void read_timers (SOLFEC *sol)
{
  clean_timers (sol); /* zero total timing */

#if 0 /* HDF5 */
  for (PBF *bf = sol->bf; bf; bf = bf->next)
  {
    PBF_Push (bf, "TIMERS");

    int n = H5Aget_num_attrs (bf->stack [bf->top]); /* FIXME: don't use HDF5 outside of pbf.c */

    for (int i = 0; i < n; i ++)
    {
      char name [64];
      double value;
      hid_t attr;
      TIMING *t;

      ASSERT ((attr = H5Aopen_idx (bf->stack [bf->top], i)) >= 0, ERR_PBF_READ);
      ASSERT (H5Aget_name (attr, 64, name) >= 0, ERR_PBF_READ);
      ASSERT (H5Aclose (attr) >= 0, ERR_PBF_READ);
      PBF_Double2 (bf, name, &value, 1);

      if (!(t = MAP_Find (sol->timers, name, (MAP_Compare) strcmp))) /* add timer if missing */
      {
	ERRMEM (t = MEM_Alloc (&sol->timemem));
	MAP_Insert (&sol->mapmem, &sol->timers, name, t, (MAP_Compare) strcmp);
      }

      t->total = MAX (t->total, value); /* update to maximum across all processors */
    }

    PBF_Pop (bf);
  }
#else
  int n, numt, found = 0;
  for (PBF *bf = sol->bf; bf; bf = bf->next)
  {
    if (PBF_Label (bf, "TIMERS"))
    {
      found = 1;

      PBF_Int (bf, &numt, 1);
      
      for (n = 0; n < numt; n ++)
      {
	double total;
	char *label;
	TIMING *t;

	PBF_Double (bf, &total, 1); /* read timing */
	label = NULL; /* needs allocation */
	PBF_String (bf, &label); /* read label */

	if (!(t = MAP_Find (sol->timers, label, (MAP_Compare) strcmp))) /* add timer if missing */
	{
	  ERRMEM (t = MEM_Alloc (&sol->timemem));
	  MAP_Insert (&sol->mapmem, &sol->timers, label, t, (MAP_Compare) strcmp);
	}

	t->total = MAX (t->total, total); /* update to maximum across all processors */
      }
    }
  }
  ASSERT (found, ERR_FILE_FORMAT); /* the former root file should have this section */
#endif
}

/* input state */
static void read_state (SOLFEC *sol)
{
  /* read time */

  PBF_Time (sol->bf, &sol->dom->time); /* the only domain member red outside of it */

  /* read initial flags */

  if (sol->iover < 0)
  {
    ASSERT (PBF_Label (sol->bf, "IOVER"), ERR_FILE_FORMAT);
    PBF_Int (sol->bf, &sol->iover, 1);
  }

  /* read domain */

  DOM_Read_State (sol->dom, sol->bf);

  /* read timers */

  read_timers (sol);
}

/* read initial state if needed */
static int init (SOLFEC *sol)
{
  if (sol->iover < 0)
  {
    read_state (sol);
    return 0;
  }
  else return 1;
}

/* initial statistics */
static void initstatsout (DOM *dom)
{
#if MPI
  int inp[] = {dom->dofs, dom->nbod, dom->nobs, dom->nrig, dom->nprb, dom->nfem}, vec[6];

  MPI_Reduce (inp, vec, 6, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

  if (dom->rank == 0)
#else
  int vec[] = {dom->dofs, dom->nbod, dom->nobs, dom->nrig, dom->nprb, dom->nfem};
#endif
  {
    printf ("----------------------------------------------------------------------------------------\n");
    printf ("INITIALLY DOFS %d, BODIES: %d (OBS: %d, RIG: %d, PRB: %d, FEM: %d)\n",
	    vec [0], vec [1], vec [2], vec [3], vec [4], vec [5]);
    printf ("----------------------------------------------------------------------------------------\n");
  }
}

/* test whether stop file exists */
static short stopfile (SOLFEC *sol)
{
  char *stop_path;
  FILE *stop_file;
  short stop;

  ERRMEM (stop_path = malloc (strlen (sol->outpath) + 64));
  sprintf (stop_path, "%s/STOP", sol->outpath);
  if ((stop_file = fopen (stop_path, "r")))
  {
    fclose (stop_file);
    stop = 1;
  }
  else stop = 0;

  free (stop_path);

#if MPI
  stop = PUT_int_max (stop);
#endif

  return stop;
}

/* output statistics */
static void statsout (SOLFEC *sol)
{
  DOM *dom = sol->dom;
  time_t timer = time(NULL);
  double  elapsed = difftime (timer, sol->start), /* elapsed wall clock time for this run */
	  estimated = (elapsed / (sol->dom->time  - sol->t0)) * sol->duration - elapsed; /* estimated remaining time */
  int days = (int) (estimated / 86400.),
      hours = (int) ((estimated - days * 86400.) / 3600.),
      minutes = (int) ((estimated - days * 86400. - hours * 3600.) / 60.),
      seconds = (int) (estimated - days * 86400. - hours * 3600. - minutes * 60.);
  double dtimint, dcondet, dconupd, dlocdyn, dconsol, dparbal, dtotal;
  int timint, condet, conupd, locdyn, consol, parbal, i;
  char *stapath;
  char *string;
  FILE *sta;

  string = ctime (&timer); 

#if MPI
  if (dom->rank == 0)
  {
#endif
    dtimint = SOLFEC_Timing (sol, "TIMINT");
    dconupd = SOLFEC_Timing (sol, "CONUPD");
    dcondet = SOLFEC_Timing (sol, "CONDET");
    dlocdyn = SOLFEC_Timing (sol, "LOCDYN");
    dconsol = SOLFEC_Timing (sol, "CONSOL");
    dparbal = SOLFEC_Timing (sol, "PARBAL");

    dtotal = dtimint + dconupd + dcondet + dlocdyn + dconsol + dparbal;

    timint = round (100.0 * dtimint / dtotal);
    conupd = round (100.0 * dconupd / dtotal);
    condet = round (100.0 * dcondet / dtotal);
    locdyn = round (100.0 * dlocdyn / dtotal);
    consol = round (100.0 * dconsol / dtotal);
    parbal = round (100.0 * dparbal / dtotal);


    ERRMEM (stapath = malloc (strlen (sol->outpath) + 64));
    sprintf (stapath, "%s/STATE", sol->outpath);
    ASSERT (sta = fopen (stapath, "w"), ERR_FILE_OPEN);

    fprintf (sta, "----------------------------------------------------------------------------------------\n");
    fprintf (sta, "%sEstimated end in %d days, %d hours, %d minutes and %d seconds\n", string, days, hours, minutes, seconds);
    fprintf (sta, "----------------------------------------------------------------------------------------\n");
    fprintf (sta, "TIME: %g\n", sol->dom->time);
    fprintf (sta, "----------------------------------------------------------------------------------------\n");
    printf ("----------------------------------------------------------------------------------------\n");

#if MPI
    for (i = 0; i < dom->nstats; i ++) 
    {
      fprintf (sta, "%13s: SUM = %8d     MIN = %8d     AVG = %8d     MAX = %8d\n", dom->stats [i].name, dom->stats [i].sum, dom->stats [i].min, dom->stats [i].avg, dom->stats [i].max);
      printf ("%13s: SUM = %8d     MIN = %8d     AVG = %8d     MAX = %8d\n", dom->stats [i].name, dom->stats [i].sum, dom->stats [i].min, dom->stats [i].avg, dom->stats [i].max); 
    }
#else
    int val [] = {dom->nbod, dom->aabb->boxnum, dom->ncon, dom->nspa};
    char *name [] = {"BODIES", "BOXES", "CONSTRAINTS", "SPARSIFIED"};
    for (i = 0; i < 4; i ++)
    {
      fprintf (sta, "%11s: %8d\n", name [i], val [i]);
      printf ("%11s: %8d\n", name [i], val [i]);
    }
#endif

    fprintf (sta, "----------------------------------------------------------------------------------------\n");
    printf ("----------------------------------------------------------------------------------------\n");
    fprintf (sta, "TIMINT: %2d%%, CONUPD: %2d%%, CONDET: %2d%%, LOCDYN: %2d%%, CONSOL: %2d%%, PARBAL: %2d%%\n", timint, conupd, condet, locdyn, consol, parbal);
    printf ("TIMINT: %2d%%, CONUPD: %2d%%, CONDET: %2d%%, LOCDYN: %2d%%, CONSOL %2d%%, PARBAL: %2d%%\n", timint, conupd, condet, locdyn, consol, parbal);
    fprintf (sta, "----------------------------------------------------------------------------------------\n");
    printf ("----------------------------------------------------------------------------------------\n");
    printf ("            Estimated end in %2d days, %2d hours, %2d minutes and %2d seconds\n", days, hours, minutes, seconds);
    printf ("========================================================================================\n");

    fclose (sta);
    free (stapath);

#if PSCTEST
    printf ("WARNING: Parallel slef-consitency checks are enabled (PSCTEST), rendering Solfec much less efficient!\n");
    printf ("=====================================================================================================\n");
#endif

#if MPI
  }
#endif
}

/* invoke constraint solver */
static void SOLVE (SOLFEC *sol, void *solver, SOLVER_KIND kind, LOCDYN *ldy, int verbose)
{
  SOLFEC_Timer_Start (sol, "CONSOL");

#if MPI
  if (ldy->dom->rank == 0)
#endif
  if (verbose) printf ("SOLVER ...\n");

  switch (kind)
  {
  case GAUSS_SEIDEL_SOLVER: GAUSS_SEIDEL_Solve (solver, ldy); break;
  case PENALTY_SOLVER: PENALTY_Solve (solver, ldy); break;
  case NEWTON_SOLVER: NEWTON_Solve (solver, ldy); break;
#if WITHSICONOS
  case SICONOS_SOLVER: SICONOS_Solve (solver, ldy); break;
#endif
  case TEST_SOLVER: TEST_Solve (solver, ldy); break;
  default: break;
  }

  SOLFEC_Timer_End (sol, "CONSOL");
}

/* create a solfec instance */
SOLFEC* SOLFEC_Create (short dynamic, double step, char *outpath)
{
  SOLFEC *sol;

  ERRMEM (sol = malloc (sizeof (SOLFEC)));
  sol->aabb = AABB_Create (DEFSIZE);
  sol->fis = FISET_Create ();
  sol->sps = SPSET_Create ();
  sol->mat = MATSET_Create ();
  sol->dom = DOM_Create (sol->aabb, sol->sps, dynamic, step);
  sol->dom->solfec = sol;

  sol->outpath = copyoutpath (outpath);
  sol->output_interval = 0;
  sol->output_time = 0;
  sol->compression = PBF_OFF;

  sol->bcd = NULL;

#if MPI
  sol->bf = readoutpath (sol->outpath);

  MPI_Barrier(MPI_COMM_WORLD);

  if (sol->bf)
  {
    if (CONTINUE_WRITE_FLAG()) /* check rank correctness of continued compuration mode */
    {
      PBF *bf = sol->bf;
      int ranks = 0, size;
      for (; bf; bf = bf->next) ranks ++;
      MPI_Comm_size (MPI_COMM_WORLD, &size);
      ASSERT_TEXT (sol->bf->parallel == PBF_ON && ranks == size, "[-c] MPI mode: %d ranks must be used to continue computation", ranks);
      PBF_Close (sol->bf);
      sol->bf = NULL;
      sol->mode = SOLFEC_WRITE;
      goto out;
    }
    else
    {
      PBF_Close (sol->bf);
      fprintf (stderr, "WARNING: Valid output files exist at path: %s\n", sol->outpath);
      fprintf (stderr, "WARNING: Remove those files manually or use a diffrent path.\n");
      fprintf (stderr, "WARNING: The MPI run of Solfec will terminate now...\n");
      EXIT(1);
    }
  }
#else
  if (!WRITE_MODE_FLAG() && (sol->bf = readoutpath (sol->outpath))) sol->mode = SOLFEC_READ;
  else if (WRITE_MODE_FLAG() && (sol->bf = readoutpath (sol->outpath)))
  {
    PBF_Close (sol->bf);
    sol->bf = NULL;
    if (CONTINUE_WRITE_FLAG())
    {
      sol->mode = SOLFEC_WRITE;
      goto out;
    }
    else
    {
      fprintf (stdout, "WARNING: Valid output files exist at path: %s\n", sol->outpath);
      char choice = 'n';
      fprintf (stdout, "Would you like to overwrite them? y/[n]:");
      fseek (stdin, 0, SEEK_END); /* this helps when SOLFEC is created multiple times --> */
      scanf ("%c", &choice); /* --> in which case scanf will correctly read new choice every time */
      if (choice == 'y') goto write;
      else
      {
	fprintf (stdout, "Solfec will terminate now...\n");
	EXIT(1);
      }
    }
  }
write:
#endif
  if (sol->bf == NULL) /* write from scratch */
  {
    if ((sol->bf = writeoutpath (sol->outpath, PBF_OFF)))
    {
      char *copy, *path = getpath (sol->outpath);

      ERRMEM (copy = malloc (strlen (path) + 8));
      sprintf (copy, "%s.py", path);
      copyfile (INPUT_FILE (), copy);
      free (copy);
      free (path);

      sol->mode = SOLFEC_WRITE;

#if HDF5
      PBF_Close (sol->bf);
      sol->bf = NULL;
#endif
    }
    else THROW (ERR_FILE_OPEN);
  }

out:
  sol->iover = -IOVER; /* negative to indicate initial state */
  sol->callback_interval = DBL_MAX;
  sol->callback_time = DBL_MAX;
  sol->data = sol->call = NULL;
  sol->callback = NULL;

  MEM_Init (&sol->mapmem, sizeof (MAP), 128);
  MEM_Init (&sol->timemem, sizeof (TIMING), 128);
  sol->timers = NULL;
  sol->verbose = 1;
  sol->cleanup = 0;
  sol->registered_bases = NULL;
  sol->subsetoutput = NULL;

  return sol;
}

/* allocate file name without extension */
char* SOLFEC_Alloc_File_Name (SOLFEC *sol, int extlen)
{
  char *copy, *path = getpath (sol->outpath);

  ERRMEM (copy = malloc (strlen (path) + extlen));
  strcpy (copy, path);
  free (path);

  return copy;
}

/* start a labeled timer */
void SOLFEC_Timer_Start (SOLFEC *sol, const char *label)
{
  TIMING *t;

  if (!(t = MAP_Find (sol->timers, (void*) label, (MAP_Compare) strcmp)))
  {
    ERRMEM (t = MEM_Alloc (&sol->timemem));
    MAP_Insert (&sol->mapmem, &sol->timers, (void*) label, t, (MAP_Compare) strcmp);
  }

  timerstart (t);
}

/* end a labeled timer (labeled timers are written to the output) */
void SOLFEC_Timer_End (SOLFEC *sol, const char *label)
{
  TIMING *t;

  if ((t = MAP_Find (sol->timers, (void*) label, (MAP_Compare) strcmp)))
  {
    timerend (t);
  }
}

/* get timing of a labeled timer */
double SOLFEC_Timing (SOLFEC *sol, const char *label)
{
  TIMING *t;

  if ((t = MAP_Find (sol->timers, (void*) label, (MAP_Compare) strcmp))) return t->total;

  return 0.0;
}

/* test whether a labeled timer exists */
int SOLFEC_Has_Timer (SOLFEC *sol, const char *label)
{
  if (MAP_Find (sol->timers, (void*) label, (MAP_Compare) strcmp)) return 1;
  else return 0;
}

/* solfec mode string */
char* SOLFEC_Mode (SOLFEC *sol)
{
  switch (sol->mode)
  {
  case SOLFEC_WRITE: return "WRITE";
  case SOLFEC_READ: return "READ";
  }

  return NULL;
}

/* run analysis with a specific constraint solver */
void SOLFEC_Run (SOLFEC *sol, SOLVER_KIND kind, void *solver, double duration)
{
  if (sol->mode == SOLFEC_WRITE)
  {
    int verbose, dodel;
    LOCDYN *ldy;

    /* set current solver */
    sol->solver = solver;
    sol->kind = kind;

#if MPI
    /* make sure that all nodes execute callback */
    int mincallback = PUT_int_min (sol->callback ? 1 : 0);
    WARNING (!(mincallback == 0 && sol->callback),
      "The CALLBACK has not been defined on all processors and it will be ignored.\n");
    if (mincallback == 0 && sol->callback) sol->callback = NULL;

    /* these values might differ due to clumsy input scripting: make them uniform */
    sol->callback_interval = PUT_double_min (sol->callback_interval);
    sol->output_interval = PUT_double_min (sol->output_interval);
    sol->callback_time = PUT_double_min (sol->callback_time);
    sol->output_time = PUT_double_min (sol->output_time);
    sol->duration = PUT_double_min (sol->duration);
    /* TODO: make this more efficient by using a single call */

#endif
    if (sol->dom->time == 0.0)
    {
      DOM_Initialize (sol->dom);

      if (sol->verbose > 0) initstatsout (sol->dom); /* print out initial statistics */

      sol->start = time (NULL);

      verbose = verbose_on (sol);

      timerstart (&sol->verbose_timing);

#if HDF5
      if (CONTINUE_WRITE_FLAG())
      {
	/* note that sol->bf is only temporarily set up and 
	 * appended during inside of write_state with HDF5 output */

        double s, e;
	PBF *bf;

        if ((bf = readoutpath (sol->outpath)))
	{
	  PBF_Limits (bf, &s, &e);
          PBF_Seek (bf, e);
          ASSERT_TEXT (dom_init_state (sol->dom, bf, NULL) > 0, "Re-initialisation of state has failed");
	  sol->dom->time = e; /* current time at the end */
	  duration -= e; /* duration is less by the time shift */
	  PBF_Close (bf);
	  sol->bf = NULL;
	}
	else write_state (sol, solver, kind); /* no previous results => save initial frame */
      }
      else /* calculation from time 0.0 hence ... */
#endif
      write_state (sol, solver, kind); /* ... save initial frame */
    }
#if HDF5
    else if (CONTINUE_WRITE_FLAG()) /* possibilities are too complex in case of multiple RUN commands per input file */
    {
      ASSERT_TEXT (0, "Multiple RUN commands are not allowed in case of a continued [-c] analysis");
    }
#endif

    if (duration > 0.0)
    {
      sol->duration = duration;
      sol->t0 = sol->dom->time;
    }

    if (sol->verbose > 0) verbose = sol->dom->verbose;
    else verbose = 0;
    dodel = 0;

    for (double t0 = sol->dom->time; sol->dom->time < (t0 + fabs(duration));)
    {
#if MPI
      if (sol->dom->rank == 0)
#endif
      if (verbose) printf ("TIME: %g ... ", sol->dom->time);

      if (sol->verbose < 0 && duration >= 0) /* % */
      {
	if (dodel) printf ("\b\b\b\b");
	int progress = (int) (100. * ((sol->dom->time - t0) / fabs(duration)));
	printf ("%3d%%", progress); dodel = 1; fflush (stdout);
      }

      /* begin update of domain */
      ldy = DOM_Update_Begin (sol->dom);

      /* begin update of local dynamics */
      LOCDYN_Update_Begin (ldy);

      /* solve constraints */
      SOLVE (sol, solver, kind, ldy, verbose);

      /* end update of local dynamics */
      LOCDYN_Update_End (ldy);

      /* end update of domain */
      DOM_Update_End (sol->dom);

      /* statistics are printed every
       * human perciveable period of time */
      if (verbose) statsout (sol), verbose = verbose_off (sol);
      if (timerend (&sol->verbose_timing) >= VERBOSITY_INTERVAL())
	verbose = verbose_on (sol), timerstart (&sol->verbose_timing);

      /* write output if needed */
      if (sol->dom->time >= sol->output_time)
      {
	sol->output_time += sol->output_interval;
	write_state (sol, solver, kind);
      }

      /* execute callback if needed */
      if (sol->callback && sol->dom->time >= sol->callback_time)
      {
	int ret;

	sol->callback_time += sol->callback_interval;
	ret = sol->callback (sol, sol->data, sol->call);
#if MPI
	ret = PUT_int_min (ret);
#endif
	if (!ret) break; /* interrupt run */
      }

      /* BCD sampling */
      if (sol->bcd) BCD_Sample (sol, sol->bcd);

      /* subset output */
      for (SUBSET_OUTPUT *sub = sol->subsetoutput; sub; sub = sub->next)
      {
        if (sub->now < sub->ntimes && sol->dom->time >= sub->times[sub->now])
	{
	  DOM_Write_Subset (sol->dom, sub->subset, sub->bf);
	}
      }

      /* check whether STOP file was created by the user */
      if (stopfile (sol)) break;
    }

    if (sol->verbose < 0 && duration >= 0) /* % */
    {
      printf ("\n");
      fflush (stdout);
    }

    /* BCD append Python output at the end of run */
    if (sol->bcd) BCD_Append_Output (sol->bcd);
  }
  else /* READ */
  {
    if (init (sol))
    {
      PBF_Forward (sol->bf, 1);
      read_state (sol);
    }
  }
}

/* set results output interval */
void SOLFEC_Output (SOLFEC *sol, double interval, PBF_FLG compression)
{
  sol->output_interval = interval;
  sol->output_time = sol->dom->time + interval;
  sol->compression = compression;
}

/* the next time minus the current time */
double SOLFEC_Time_Skip (SOLFEC *sol)
{
  if (sol->mode == SOLFEC_READ)
  {
    double t;

    init (sol);
    PBF_Forward (sol->bf, 1);
    PBF_Time (sol->bf, &t);
    PBF_Backward (sol->bf, 1);
    return t - sol->dom->time;
  }
  else return sol->dom->step;
}

/* initialize solfec in READ state */
void SOLFEC_Read_Init (SOLFEC *sol)
{
  if (sol->mode == SOLFEC_READ) init (sol);
}

/* get analysis duration time limits */
void SOLFEC_Time_Limits (SOLFEC *sol, double *start, double *end)
{
  if (sol->mode == SOLFEC_READ)
  {
    double s, e;

    init (sol);
    PBF_Limits (sol->bf, &s, &e);
    if (start) *start = s;
    if (end) *end = e;
  }
  else 
  {
    if (start) *start = 0;
    if (end) *end = sol->dom->time;
  }
}

/* set up callback function */
void SOLFEC_Set_Callback (SOLFEC *sol, double interval, void *data, void *call, SOLFEC_Callback callback)
{
  sol->callback_interval = interval;
  sol->callback_time = sol->dom->time;
  sol->data = data;
  sol->call = call;
  sol->callback = callback;
}

/* seek to specific time in READ mode */
void SOLFEC_Seek_To (SOLFEC *sol, double time)
{
  if (sol->mode == SOLFEC_READ)
  {
    init (sol);
    PBF_Seek (sol->bf, time);
    read_state (sol);
  }
}

/* step backward in READ modes */
int SOLFEC_Backward (SOLFEC *sol, int steps)
{
  if (sol->mode == SOLFEC_READ)
  {
    if (init (sol))
    {
      int ret = PBF_Backward (sol->bf, steps);
      read_state (sol);
      return ret;
    }
  }

  return 0;
}

/* step forward in READ modes */
int SOLFEC_Forward (SOLFEC *sol, int steps, int corotated_displacements)
{
  if (sol->mode == SOLFEC_READ)
  {
    if (init (sol))
    {
      int ret = PBF_Forward (sol->bf, steps);

      read_state (sol);

      if (corotated_displacements && sol->bcd)
      {
	BCD_Sample (sol, sol->bcd);
	BCD_Append_Output (sol->bcd);
      }

      return ret;
    }
  }

  return 0;
}

/* read the history of an object (a labeled value, a body or
 * a constraint) and invoke the callback for every new state */
/* perform abort actions */
void SOLFEC_Abort (SOLFEC *sol)
{
  if (sol->bf)
  {
#if !HDF5
    PBF_Close (sol->bf);
#endif
    sol->bf = NULL;
  }
}

/* free solfec memory */
void SOLFEC_Destroy (SOLFEC *sol)
{
  if (sol->mode == SOLFEC_WRITE && sol->iover < 0)
  {
#if POSIX
    if (sol->cleanup) rmrf (sol->outpath); /* remove output directory */
#endif
  }

  BCD_Destroy (sol->bcd);
  AABB_Destroy (sol->aabb); 
  FISET_Destroy (sol->fis);
  SPSET_Destroy (sol->sps);
  MATSET_Destroy (sol->mat);
  DOM_Destroy (sol->dom);

  free (sol->outpath);

#if !HDF5
  if (sol->bf) PBF_Close (sol->bf);
#endif

  if (sol->mode == SOLFEC_READ)
  {
    for (MAP *item = MAP_First (sol->timers); item; item = MAP_Next (item))
    {
      free (item->key); /* labels were allocated in READ mode */
    }
#if HDF5
      PBF_Close (sol->bf);
#endif
  }

  for (MAP *item = MAP_First (sol->registered_bases); item; item = MAP_Next (item))
  {
    FE_BASE *base = item->data;
    MX_Destroy (base->evec);
    free (base->eval);
    free (base->label);
    free (base);
  }

  MEM_Release (&sol->mapmem);
  MEM_Release (&sol->timemem);

  SUBSET_OUTPUT *sub = sol->subsetoutput, *nsub = NULL;
  for (; sub; sub = nsub)
  {
    free (sub->times); 
    nsub = sub->next;
    free (sub);
  }

  free (sol);
}

/* read histories of a set of requested items; allocate and fill 'history'  members
 * of those items; return table of times of the same 'size' as the 'history' members;
 * skip every 'skip' steps; if 'skip' < 0 then print out a percentage based progress bar */
double* SOLFEC_History (SOLFEC *sol, SHI *shi, int nshi, double t0, double t1, int skip, int *size)
{
  if (sol->mode == SOLFEC_WRITE) return NULL;

  double save, *time;
  int cur, i,
      dodel = 0,
      timers = 0,
      labeled = 0,
      full_read = 0;

  if (skip < 0) printf ("Reading history ... "); /* progress begin */

  cur = 0;
  save = sol->dom->time;
  SOLFEC_Seek_To (sol, t0);
  *size = PBF_Span (sol->bf, t0, t1) / ABS (skip);
  ERRMEM (time = MEM_CALLOC (sizeof (double [(*size) + 4]))); /* safeguard */
  time [cur] = sol->dom->time;

  for (i = 0; i < nshi; i ++)
  {
    ERRMEM (shi[i].history = MEM_CALLOC (sizeof (double [(*size) + 4])));

    switch (shi [i].item)
    {
      case BODY_ENTITY:
      case ENERGY_VALUE: full_read = 1; break;
      case TIMING_VALUE: timers = 1; break;
      case CONSTRAINT_VALUE: full_read = 1; break;
      case LABELED_INT:
      case LABELED_DOUBLE: labeled = 1; break;
    }
  }

  do
  {
    for (i = 0; i < nshi; i ++)
    {
      switch (shi[i].item)
      {
      case BODY_ENTITY:
	{
	  double values [7];
	  if (shi[i].bod)
	  {
	    BODY_Point_Values (shi[i].bod, shi[i].point, shi[i].entity, values);
	    shi[i].history [cur] = values [shi[i].index];
	  }
	  else
	  {
	    ASSERT_DEBUG (shi[i].label, "BUG: history iterm BODY label expexted but NULL found");
	    BODY *bod = MAP_Find (sol->dom->lab, shi[i].label, (MAP_Compare)strcmp);
	    if (bod)
	    {
	      BODY_Point_Values (bod, shi[i].point, shi[i].entity, values);
	      shi[i].history [cur] = values [shi[i].index];
	    }
	    else shi[i].history [cur] = NAN;
	  }
	}
	break;
      case ENERGY_VALUE:
	{
	  if (shi [i].bodies)
	  {
	    for (SET *item = SET_First (shi[i].bodies); item; item = SET_Next (item))
	    {
	      BODY *bod = item->data;
	      shi[i].history [cur] += bod->energy [shi[i].index];
	    }
	  }
	  else
	  {
	    for (BODY *bod = sol->dom->bod; bod; bod = bod->next)
	    {
	      shi[i].history [cur] += bod->energy [shi[i].index];
	    }
	  }
	}
	break;
      case TIMING_VALUE:
	{
          shi[i].history [cur] = SOLFEC_Timing (sol, shi[i].label);
	}
	break;
      case CONSTRAINT_VALUE:
	{
	  double value, vec [3],
		 div = 1.0,
	        *dir = shi[i].vector;
	  int s1 = shi[i].surf1,
	      s2 = shi[i].surf2;
	  short usedir = DOT (dir, dir) > 0.0 ? 1 : 0;
	  SET *conset = NULL;
	  MEM conmem;

	  MEM_Init (&conmem, sizeof (SET), 128);

	  switch (shi[i].op)
	  {
	  case OP_SUM:
	  case OP_AVG: value = 0.0; break;
	  case OP_MAX: value = -DBL_MAX; break;
	  case OP_MIN: value = DBL_MAX; break;
	  }

	  if (shi [i].bodies)
	  {
	    for (SET *item = SET_First (shi[i].bodies); item; item = SET_Next (item))
	    {
	      BODY *bod = item->data;
	      for (SET *jtem = SET_First (bod->con); jtem; jtem = SET_Next (jtem))
	      {
		SET_Insert (&conmem, &conset, jtem->data, NULL); /* collect constraints (avoid duplicates) */
	      }
	    }
	  }
	  else
	  {
	    for (BODY *bod = sol->dom->bod; bod; bod = bod->next)
	    {
	      for (SET *jtem = SET_First (bod->con); jtem; jtem = SET_Next (jtem))
	      {
		SET_Insert (&conmem, &conset, jtem->data, NULL); /* collect constraints (avoid duplicates) */
	      }
	    }
	  }

	  for (SET *jtem = SET_First (conset); jtem; jtem = SET_Next (jtem))
	  {
	    CON *con = jtem->data;
	    double *conbase = con->base;

	    if (shi[i].contacts_only && con->kind != CONTACT) continue;
	    else if (con->kind == CONTACT && s1 != INT_MAX && s2 != INT_MAX)
	    {
	      int r1 = con->spair[0], r2 = con->spair[1];
	      if (!((s1 == r1 && s2 == r2) || (s1 == r2 && s2 == r1))) continue;
	    }

	    switch (shi[i].index)
	    {
	    case CONSTRAINT_GAP:
	      if (con->kind == CONTACT)
	      {
		value = MIN (value, con->gap); 
	      }
	      break;
	    case CONSTRAINT_R:
	      if (usedir)
	      {
		TVMUL (conbase, con->R, vec);
		value += DOT (dir, vec);
	      }
	      else value += con->R[2];
	      break;
	    case CONSTRAINT_U:
	      if (usedir)
	      {
		TVMUL (conbase, con->U, vec);
		value += DOT (dir, vec);
	      }
	      else value += con->U[2];
	      div += 1.0;
	      break;
	    }
	  }

	  if (fabs (value) == DBL_MAX) value = 0.0;

          shi[i].history [cur] = value / div;

	  MEM_Release (&conmem);
	}
	break;
      case LABELED_DOUBLE:
      case LABELED_INT:
	{
	  int ival = 0;
	  double dval, total, num = 0;

	  switch (shi [i].op)
	  {
	    case OP_SUM: total = 0.0; break;
	    case OP_AVG: total = 0.0; break;
	    case OP_MIN: total = DBL_MAX; break;
	    case OP_MAX: total = -DBL_MAX; break;
	  }

	  for (PBF *bf = sol->bf; bf; bf = bf->next)
	  {
	    if (PBF_Label (bf, shi [i].label))
	    {
	      if (shi [i].item == LABELED_INT) { PBF_Int (bf, &ival, 1); dval = ival; }
	      else PBF_Double (bf, &dval, 1);

	      switch (shi [i].op)
	      {
		case OP_SUM: total += dval; break;
		case OP_AVG: total += dval; break;
		case OP_MIN: total = MIN (dval, total); break;
		case OP_MAX: total = MAX (dval, total); break;
	      }

	      num += 1.0;
	    }
	  }

	  switch (shi [i].op)
	  {
	    case OP_SUM: case OP_MIN: case OP_MAX: break;
	    case OP_AVG: total = (num > 0.0 ? total / num : 0.0); break;
	  }

	  shi[i].history [cur] = total;
	}
	break;
      }
    }

    time [cur ++] = sol->dom->time; /* store current time and iterate */

    if (full_read) SOLFEC_Forward (sol, ABS (skip), 0); /* read complete Solfec state */
    else
    {
      PBF_Forward (sol->bf, ABS (skip)); /* move to next frame */

      PBF_Time (sol->bf, &sol->dom->time); /* read time */

      if (labeled) read_state (sol); /* read whole domain */

      if (timers) read_timers (sol); /* read timers */
    }

    if (skip < 0) /* print out progress */
    {
      if (dodel) printf ("\b\b\b\b");
      int progress = (int) (100. * ((sol->dom->time - t0) / (t1 - t0)));
      printf ("%3d%%", progress); dodel = 1; fflush (stdout);
    }
  }
  while (sol->dom->time < t1);

  SOLFEC_Seek_To (sol, save); /* restore initial time frame */

  if (skip < 0) printf ("\n"); /* progress end */

  return time;
}

/* export MBFCP definition*/
void SOLFEC_2_MBFCP (SOLFEC *sol, FILE *out)
{
  SPSET_2_MBFCP (sol->sps, out);

  MATSET_2_MBFCP (sol->mat, out);

  DOM_2_MBFCP (sol->dom, out);
}

/* initialize state from the ouput; return 1 on success, 0 otherwise;
 * optinal "subset" is a set of strings defining POSIX regular expressions to be matched
 * against body labels -- narrowing down the set of bodies whose state will  be initialized */
int SOLFEC_Initialize_State (SOLFEC *sol, char *path, double time, SET *subset)
{
  int ret;
  PBF *bf;

  WARNING (bf = readoutpath (path), "Opening of the output files has failed.");
  if (!bf) return 0;

  PBF_Seek (bf, time);
  ret = dom_init_state (sol->dom, bf, subset);
  PBF_Close (bf);

  return ret;
}

/* map rigid motion onto FEM bodies; return 1 on success, 0 otherwise;
 * optinal "subset" is a set of strings defining POSIX regular expressions to be matched
 * against body labels -- narrowing down the set of bodies whose state will  be initialized */
int SOLFEC_Rigid_To_FEM (SOLFEC *sol, char *path, double time, SET *subset)
{
  int ret;
  PBF *bf;

  WARNING (bf = readoutpath (path), "Opening of the output files has failed.");
  if (!bf) return 0;

  PBF_Seek (bf, time);
  ret = dom_rigid_to_fem (sol->dom, bf, subset);
  PBF_Close (bf);

  return ret;
}

/* register FE base; (evec, eval, label) must be dynamically allocated */
void SOLFEC_Register_Base (SOLFEC *sol, MX *evec, double *eval, char *label)
{
  FE_BASE *base;

  ERRMEM (base = malloc (sizeof (FE_BASE)));
  base->evec = evec;
  base->eval = eval;
  base->label = label;

  MAP_Insert (&sol->mapmem, &sol->registered_bases, label, base, (MAP_Compare)strcmp);
}

/* register subset output specification */
void SOLFEC_Register_Subset_Output (SOLFEC *sol, double *times, int ntimes, char *path, char *subset)
{
  SUBSET_OUTPUT *sub = MEM_CALLOC (sizeof(SUBSET_OUTPUT));
  sub->times = times;
  sub->now = 0;
  sub->ntimes = ntimes;
  sub->path = path;
  sub->subset = subset;
  sub->bf = NULL; /* FIXME */
  sub->next = sol->subsetoutput;
  sol->subsetoutput = sub;
}
