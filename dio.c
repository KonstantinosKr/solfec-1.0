/*
 * dio.c
 * Copyright (C) 2009, Tomasz Koziara (t.koziara AT gmail.com)
 * ---------------------------------------------------------------
 * domain input-output
 */

#if POSIX
#include <sys/stat.h>
#include <regex.h>
#endif
#include <string.h>
#include "sol.h"
#include "dio.h"
#include "pck.h"
#include "err.h"

/* write constraint state */
static void write_constraint (CON *con, PBF *bf)
{
  unsigned int zero = 0;
  int kind = con->kind;

  PBF_Uint (bf, &con->id, 1);
  PBF_Int (bf, &kind, 1);

  PBF_Double (bf, con->R, 3);
  PBF_Double (bf, con->U, 3);
#if IOVER > 1
  if (kind == CONTACT)
  {
    PBF_Double (bf, con->V, 3);
  }
#endif
  PBF_Double (bf, con->point, 3);
  PBF_Double (bf, con->base, 9);
  PBF_Double (bf, &con->merit, 1);

  PBF_Uint (bf, &con->master->id, 1);
  if (con->slave) PBF_Uint (bf, &con->slave->id, 1);
  else PBF_Uint (bf, &zero, 1);

  if (kind == CONTACT)
  {
    SURFACE_MATERIAL_Write_State (&con->mat, bf);
    PBF_Double (bf, &con->area, 1);
    PBF_Double (bf, &con->gap, 1);
    PBF_Int (bf, con->spair, 2);
  }

  if (kind == RIGLNK ||
      kind == VELODIR ||
      kind == SPRING) PBF_Double (bf, con->Z, DOM_Z_SIZE);

#if MPI
  PBF_Int (bf, &con->master->dom->rank, 1);
#endif
}

/* read constraint state */
static CON* read_constraint (DOM *dom, int iover, PBF *bf)
{
  SOLFEC_MODE mode = dom->solfec->mode;
  unsigned int id;
  CON *con;
  int kind;

  ERRMEM (con = MEM_Alloc (&dom->conmem));

  PBF_Uint (bf, &con->id, 1);
  PBF_Int (bf, &kind, 1);
  con->kind = kind;

  PBF_Double (bf, con->R, 3);
  PBF_Double (bf, con->U, 3);
  if (iover > 1 && kind == CONTACT)
  {
    PBF_Double (bf, con->V, 3);
  }
  PBF_Double (bf, con->point, 3);
  PBF_Double (bf, con->base, 9);
  PBF_Double (bf, &con->merit, 1);

  PBF_Uint (bf, &id, 1);
  ASSERT_DEBUG_EXT (con->master = MAP_Find (dom->allbodies, (void*) (long) id, NULL), "Invalid master id");
  PBF_Uint (bf, &id, 1);
  if (id) ASSERT_DEBUG_EXT (con->slave = MAP_Find (dom->allbodies, (void*) (long) id, NULL), "Invalid slave id");

  if (kind == CONTACT)
  {
    con->state |= SURFACE_MATERIAL_Read_State (dom->sps, &con->mat, bf);
    PBF_Double (bf, &con->area, 1);
    PBF_Double (bf, &con->gap, 1);
    PBF_Int (bf, con->spair, 2);
  }

  if (iover < 4)
  {
    if (kind == RIGLNK ||
        kind == VELODIR) PBF_Double (bf, con->Z, DOM_Z_SIZE);
  }
  else
  {
    if (kind == RIGLNK ||
        kind == VELODIR ||
	kind == SPRING) PBF_Double (bf, con->Z, DOM_Z_SIZE);
  }

  if (bf->parallel == PBF_ON)
  {
    if (mode == SOLFEC_READ)
    {
      PBF_Int (bf, &con->rank, 1);
    }
    else /* fake it => ranks are actually used in WRITE mode */
    {
      int rank;
      PBF_Int (bf, &rank, 1);
    }
  }

  return con;
}

/* attach constraints to bodies after reading */
static void dom_attach_constraints (DOM *dom)
{
  BODY *bod;
  CON *con;

  for (bod = dom->bod; bod; bod = bod->next) SET_Free (&dom->setmem, &bod->con);

  for (con = dom->con; con; con = con->next)
  {
    if (con->master)
    {
      ASSERT_DEBUG (MAP_Find_Node (dom->idb, (void*) (long) con->master->id, NULL), "Invalid master id");
      SET_Insert (&dom->setmem, &con->master->con, con, NULL);
    }

    if (con->slave)
    {
      ASSERT_DEBUG (MAP_Find_Node (dom->idb, (void*) (long) con->slave->id, NULL), "Invalid slave id");
      SET_Insert (&dom->setmem, &con->slave->con, con, NULL);
    }
  }
}

/* write new bodies data */
static void write_new_bodies (DOM *dom)
{
  if (dom->newb == NULL) return; /* nothing to write */

#if HDF5
  PBF *bf = dom->solfec->bf;
  int isize = 0, ints = 0, *i = NULL;
  int dsize = 0, doubles = 0;
  double *d = NULL;
  SET *item;
  int n;

  PBF_Push (bf, "NEWBOD");

  n = SET_Size (dom->newb);
  PBF_Int2 (bf, "count", &n, 1);

  for (item = SET_First (dom->newb); item; item = SET_Next (item))
  {
    BODY_Pack (item->data, &dsize, &d, &doubles, &isize, &i, &ints);
  }

  PBF_Int2 (bf, "ints", &ints, 1);
  PBF_Int2 (bf, "i", i, ints);
  PBF_Int2 (bf, "doubles", &doubles, 1);
  PBF_Double2 (bf, "d", d, doubles);

  free (d);
  free (i);

  PBF_Pop (bf);
#else
  char *path, *ext;
  FILE *file;
  XDR xdr;

  path = SOLFEC_Alloc_File_Name (dom->solfec, 16);
  ext = path + strlen (path);

#if MPI
  sprintf (ext, ".bod.%d", dom->rank);
#else
  sprintf (ext, ".bod");
#endif

  ASSERT (file = fopen (path, "a"), ERR_FILE_OPEN);
  xdrstdio_create (&xdr, file, XDR_ENCODE);

  int isize = 0, ints, *i = NULL;
  int dsize = 0, doubles;
  double *d = NULL;
  SET *item;

  for (item = SET_First (dom->newb); item; item = SET_Next (item))
  {
    doubles = ints = 0;

    BODY_Pack (item->data, &dsize, &d, &doubles, &isize, &i, &ints);

    ASSERT (xdr_int (&xdr, &doubles), ERR_PBF_WRITE);
    ASSERT (xdr_vector (&xdr, (char*)d, doubles, sizeof (double), (xdrproc_t)xdr_double), ERR_PBF_WRITE);

    ASSERT (xdr_int (&xdr, &ints), ERR_PBF_WRITE);
    ASSERT (xdr_vector (&xdr, (char*)i, ints, sizeof (int), (xdrproc_t)xdr_int), ERR_PBF_WRITE);
  }

  free (d);
  free (i);

  xdr_destroy (&xdr);
  fclose (file);
  free (path);
#endif
}

/* read new bodies data */
static void read_new_bodies (DOM *dom, PBF *bf)
{
#if HDF5
  double time, start, end;

  PBF_Time (bf, &time); /* back up time frame from outside of this function */
  PBF_Limits (bf, &start, &end);
  PBF_Seek (bf, start);

  do
  {
    for (PBF *f = bf; f; f = f->next)
    {
      int ipos = 0, ints;
      int dpos = 0, doubles;
      double *d;
      int *i;
      int k, n;
      BODY *bod;

      if (PBF_Has_Group (f, "NEWBOD") == 0) continue; /* don't try to read if there are no new bodies stored */

      PBF_Push (f, "NEWBOD");

      PBF_Int2 (f, "count", &n, 1);
      PBF_Int2 (f, "ints", &ints, 1);
      ERRMEM (i = malloc (sizeof (int [ints])));
      PBF_Int2 (f, "i", i, ints);
      PBF_Int2 (f, "doubles", &doubles, 1);
      ERRMEM (d = malloc (sizeof (double [doubles])));
      PBF_Double2 (f, "d", d, doubles);

      for (k = 0; k < n; k ++)
      {
	bod = BODY_Unpack (dom->solfec, &dpos, d, doubles, &ipos, i, ints);

	if (!MAP_Find (dom->allbodies, (void*) (long) bod->id, NULL))
	{
	  MAP_Insert (&dom->mapmem, &dom->allbodies, (void*) (long) bod->id, bod, NULL); /* all bodies from all times */
	}
	else BODY_Destroy (bod); /* FIXME: bodies created in input files at time > 0;
				    FIXME: perhaps there is no need of moving GLV to the fist lng_RUN call,
				    FIXME: but rather bodies created in Python should not be put into the 'dom->newb' set;
				    FIXME: this way, as now, all Python created bodies will be anyway read at time 0 */
      }

      free (d);
      free (i);

      PBF_Pop (f);
    }
  } while (PBF_Forward (bf, 1));

  dom->allbodiesread = 1; /* mark as read */

  PBF_Seek (bf, time); /* seek to the backed up time again */
#else
  char *path, *ext;
  FILE *file;
  int m, n;
  XDR xdr;

  if (dom->solfec->verbose)
    printf ("Reading all bodies ...\n");

  dom->allbodiesread = 1; /* mark as read */

  path = SOLFEC_Alloc_File_Name (dom->solfec, 16);
  ext = path + strlen (path);
  
  for (m = 0; bf; bf = bf->next) m ++; /* count input files */

  for (n = 0; n < m; n ++)
  {
    if (n || m > 1)
    {
      sprintf (ext, ".bod.%d", n);
      if (!(file = fopen (path, "r"))) continue; /* no new bodies for this rank */
    }
    else /* n == 0 && m == 1 */
    {
      sprintf (ext, ".bod.%d", n);
      if (!(file = fopen (path, "r"))) /* either prallel with "mpirun -np 1" */
      {
	sprintf (ext, ".bod");
	if (!(file = fopen (path, "r"))) continue; /* or serial */
      }
    }

    xdrstdio_create (&xdr, file, XDR_DECODE);

    int ipos, ints, *i, dpos, doubles;
    double *d;
    BODY *bod;

    for (;;)
    {
      if (xdr_int (&xdr, &doubles))
      {
	ERRMEM (d = malloc (sizeof (double [doubles])));
	if (xdr_vector (&xdr, (char*)d, doubles, sizeof (double), (xdrproc_t)xdr_double))
	{
	  if (xdr_int (&xdr, &ints))
	  {
	    ERRMEM (i = malloc (sizeof (int [ints])));
	    if (xdr_vector (&xdr, (char*)i, ints, sizeof (int), (xdrproc_t)xdr_int))
	    {
	      ipos = dpos = 0;

	      bod = BODY_Unpack (dom->solfec, &dpos, d, doubles, &ipos, i, ints);

	      if (!MAP_Find (dom->allbodies, (void*) (long) bod->id, NULL))
	      {
	        MAP_Insert (&dom->mapmem, &dom->allbodies, (void*) (long) bod->id, bod, NULL);
	      }
	      else BODY_Destroy (bod); /* FIXME: bodies created in input files at time > 0;
					  FIXME: perhaps there is no need of moving GLV to the fist lng_RUN call,
					  FIXME: but rather bodies created in Python should not be put into the 'dom->newb' set;
					  FIXME: this way, as now, all Python created bodies will be anyway read at time 0 */
	      free (d);
	      free (i);
	    }
	    else
	    {
	      free (d);
	      free (i);
	      break;
	    }
	  }
	  else
	  {
	    free (d);
	    break;
	  }
	}
	else
	{
	  free (d);
	  break;
	}
      }
      else break;
    }

    xdr_destroy (&xdr);
    fclose (file);
  }

  free (path);
#endif
}

/* write domain state */
void dom_write_state (DOM *dom, PBF *bf, SET *subset)
{
  /* mark domain output */

  PBF_Label (bf, "DOM");

#if IOVER >= 3
  /* write iover */

  PBF_Label (bf, "IOVER");

  int iover  = ABS (dom->solfec->iover);

  PBF_Int (bf, &iover, 1);
#endif

  /* write time step */

  PBF_Label (bf, "STEP");

  PBF_Double (bf, &dom->step, 1);

  /* write constraints merit */

  PBF_Label (bf, "MERIT");

  PBF_Double (bf, &dom->merit, 1);

  /* write complete data of newly created bodies and empty the newly created bodies set */

  write_new_bodies (dom); /* writing is done to a separate file */

  SET_Free (&dom->setmem, &dom->newb);

  /* write regular bodies (this also includes states of newly created ones) */

  PBF_Label (bf, "BODS");

  if (subset)
  {
    int nbod = SET_Size (subset);

    PBF_Int (bf, &nbod, 1);
  }
  else PBF_Int (bf, &dom->nbod, 1);

  for (BODY *bod = dom->bod; bod; bod = bod->next)
  {
    if (subset && !SET_Find (subset, (void*) (long) bod->id, NULL)) continue;

    PBF_Uint (bf, &bod->id, 1);

    if (bod->label) PBF_Label (bf, bod->label); /* label body record for fast access */

    BODY_Write_State (bod, bf);
  }

  /* write constraints */

  PBF_Label (bf, "CONS");

  if (subset)
  {
    int ncon = 0;

    for (CON *con = dom->con; con; con = con->next)
    {
      if (subset)
      {
	if (!SET_Find (subset, (void*) (long) con->master->id, NULL)) continue;
	if (con->slave && !SET_Find (subset, (void*) (long) con->slave->id, NULL)) continue;
      }

      ncon ++;
    }

    PBF_Int (bf, &ncon, 1);
  }
  else PBF_Int (bf, &dom->ncon, 1);

  for (CON *con = dom->con; con; con = con->next)
  {
    if (subset)
    {
      if (!SET_Find (subset, (void*) (long) con->master->id, NULL)) continue;
      if (con->slave && !SET_Find (subset, (void*) (long) con->slave->id, NULL)) continue;
    }

    write_constraint (con, bf);
  }
}

/* read domain state */
void dom_read_state (DOM *dom, PBF *bf)
{
  BODY *bod, *next;
  int ncon;

  /* clear contacts */
  MAP_Free (&dom->mapmem, &dom->idc);
  MEM_Release (&dom->conmem);
  dom->con = NULL;
  dom->ncon = 0;

  /* read all bodies if needed */
  if (!dom->allbodiesread) read_new_bodies (dom, bf);

  /* mark all bodies as absent */
  for (bod = dom->bod; bod; bod = bod->next) bod->flags |= BODY_ABSENT;

  SET *usedlabel = NULL;

  for (; bf; bf = bf->next)
  {
    if (PBF_Label (bf, "DOM"))
    {
      /* read iover */

      int iover = 2;

      if (PBF_Label (bf, "IOVER"))
      {
	PBF_Int (bf, &iover, 1);
      }

      /* read time step */

      ASSERT (PBF_Label (bf, "STEP"), ERR_FILE_FORMAT);

      PBF_Double (bf, &dom->step, 1);

      /* read constraints merit */

      ASSERT (PBF_Label (bf, "MERIT"), ERR_FILE_FORMAT);

      PBF_Double (bf, &dom->merit, 1);

      /* read body states */

      ASSERT (PBF_Label (bf, "BODS"), ERR_FILE_FORMAT);

      int nbod;

      PBF_Int (bf, &nbod, 1);

      for (int n = 0; n < nbod; n ++)
      {
	unsigned int id;

	PBF_Uint (bf, &id, 1);
	bod = MAP_Find (dom->idb, (void*) (long) id, NULL);

	if (bod == NULL) /* pick from all bodies set */
	{
	  ASSERT_DEBUG_EXT (bod = MAP_Find (dom->allbodies, (void*) (long) id, NULL), "Body id invalid");

	  if (bod->label)
	  {
	    MAP *node = MAP_Find_Node (dom->lab, bod->label, (MAP_Compare)strcmp);
	    if (node)
	    {
	      node->data = bod; /* body fregments can inherit labels */
	      SET_Insert (NULL, &usedlabel, bod->label, (SET_Compare)strcmp);
	    }
	    else MAP_Insert (&dom->mapmem, &dom->lab, bod->label, bod, (MAP_Compare) strcmp);
	  }
	  MAP_Insert (&dom->mapmem, &dom->idb, (void*) (long) bod->id, bod, NULL);
	  bod->next = dom->bod;
	  if (dom->bod) dom->bod->prev = bod;
	  dom->bod = bod;
	  bod->dom = dom;
	  dom->nbod ++;
	}

	BODY_Read_State (bod, bf, iover);
	bod->flags &= ~BODY_ABSENT;
      }

      /* read constraints */

      ASSERT (PBF_Label (bf, "CONS"), ERR_FILE_FORMAT);
    
      PBF_Int (bf, &ncon, 1);

      for (int n = 0; n < ncon; n ++)
      {
	CON *con;
	
	con = read_constraint (dom, iover, bf);
	MAP_Insert (&dom->mapmem, &dom->idc, (void*) (long) con->id, con, NULL);
	con->next = dom->con;
	if (dom->con) dom->con->prev = con;
	dom->con = con;
      }

      dom->ncon += ncon;
    }
  }

  /* remove absent bodies */
  for (bod = dom->bod; bod; bod = next)
  {
    next = bod->next;

    if (bod->flags & BODY_ABSENT)
    {
      if (bod->label && !SET_Contains (usedlabel, bod->label, (SET_Compare)strcmp))
	MAP_Delete (&dom->mapmem, &dom->lab, bod->label, (MAP_Compare) strcmp);
      MAP_Delete (&dom->mapmem, &dom->idb, (void*) (long) bod->id, NULL);
      if (bod->next) bod->next->prev = bod->prev;
      if (bod->prev) bod->prev->next = bod->next;
      else dom->bod = bod->next;
      dom->nbod --;
    }
  }

  SET_Free (NULL, &usedlabel);

  /* attach constraints to bodies */
  dom_attach_constraints (dom);
}

/* read state of an individual body */
int dom_read_body (DOM *dom, PBF *bf, BODY *bod)
{
  /* read iover */

  int iover = 2;

  if (PBF_Label (bf, "IOVER"))
  {
    PBF_Int (bf, &iover, 1);
  }

  if (bod->label)
  {
    for (; bf; bf = bf->next)
    {
      if (PBF_Label (bf, bod->label))
      {
	BODY_Read_State (bod, bf, iover);
	return 1;
      }
    }
  }
  else
  {
    for (; bf; bf = bf->next)
    {
      if (PBF_Label (bf, "BODS"))
      {
	int nbod;

	PBF_Int (bf, &nbod, 1);

	for (int n = 0; n < nbod; n ++)
	{
	  unsigned int id;
	  BODY *obj;

	  PBF_Uint (bf, &id, 1);
	  ASSERT_DEBUG_EXT (obj = MAP_Find (dom->idb, (void*) (long) id, NULL), "Body id invalid");
	  if (bod->id == obj->id) 
	  {
	    BODY_Read_State (bod, bf, iover);
	    return 1;
	  }
	  else /* skip body and continue */
	  {
	    BODY fake;

	    ERRMEM (fake.conf = malloc (sizeof (double [BODY_Conf_Size (obj)])));
	    ERRMEM (fake.velo = malloc (sizeof (double [obj->dofs])));
	    fake.shape = NULL;

	    BODY_Read_State (&fake, bf, iover);

	    free (fake.conf);
	    free (fake.velo);
	  }
	}
      }
    }
  }

  return 0;
}

/* read state of an individual constraint */
int dom_read_constraint (DOM *dom, PBF *bf, CON *con)
{
  /* read iover */

  int iover = 2;

  if (PBF_Label (bf, "IOVER"))
  {
    PBF_Int (bf, &iover, 1);
  }

  /* read constraint */

  for (; bf; bf = bf->next)
  {
    int ncon;

    if (PBF_Label (bf, "CONS"))
    {
      PBF_Int (bf, &ncon, 1);

      for (int n = 0; n < ncon; n ++)
      {
	CON *obj = read_constraint (dom, iover, bf);

	if (con->id == obj->id)
	{
	  *con = *obj;
          MEM_Free (&dom->conmem, obj); /* not needed */
	  return 1;
	}
	else MEM_Free (&dom->conmem, obj); /* skip and continue */
      }
    }
  }

  return 0;
}

#if POSIX
/* get regular expression error */
static char *get_regerror (int errcode, regex_t *compiled)
{
  size_t length = regerror (errcode, compiled, NULL, 0);
  char *buffer = malloc (length);
  regerror (errcode, compiled, buffer, length);
  return buffer;
}
#endif

/* initialize domain state */
int dom_init_state (DOM *dom, PBF *bf, SET *subset)
{
  for (; bf; bf = bf->next)
  {
    if (PBF_Label (bf, "DOM"))
    {
      /* read iover */

      int iover = 2;

      if (PBF_Label (bf, "IOVER"))
      {
	PBF_Int (bf, &iover, 1);
      }
 
      ASSERT_TEXT (iover >= 3, "Output files are too old for INITIALISE_STATE to work");

      /* read body states */

      if (subset)
      {
#if POSIX
	for (SET *item = SET_First (subset); item; item = SET_Next (item))
	{
	  regex_t xp;
	  char *pattern = item->data;
	  int error = regcomp (&xp, pattern, 0);

	  if (error != 0)
	  {
	    char *message = get_regerror (error, &xp);
	    fprintf (stderr, "-->\n");
	    fprintf (stderr, "Regular expression ERROR --> %s\n", message);
	    fprintf (stderr, "<--\n");
	    regfree (&xp);
	    free (message);
	    return 0;
	  }

	  for (BODY *bod = dom->bod; bod; bod = bod->next)
	  {
	    if (bod->label && regexec (&xp, bod->label, 0, NULL, 0) == 0)
	    {
	      if (PBF_Label (bf, bod->label))
	      {
	        BODY_Read_State (bod, bf, iover);
	      }
	    }
	  }

	  regfree (&xp);
	}
#else
	ASSERT_TEXT (0, "Regular expressions require POSIX support --> recompile Solfec with POSIX=yes");
	return 0;
#endif
      }
      else
      {
	ASSERT (PBF_Label (bf, "BODS"), ERR_FILE_FORMAT);

	int nbod;

	PBF_Int (bf, &nbod, 1);

	for (int n = 0; n < nbod; n ++)
	{
	  unsigned int id;
	  BODY *bod;

	  PBF_Uint (bf, &id, 1);
	  bod = MAP_Find (dom->idb, (void*) (long) id, NULL);

	  if (bod) /* update state of existing bodies only */
	  {
	    BODY_Read_State (bod, bf, iover);
	  }
	  else /* mock read */
	  {
	    int rkind;
	    int rconf;
	    int rdofs;

	    PBF_Int (bf, &rkind, 1);
	    PBF_Int (bf, &rconf, 1);
	    PBF_Int (bf, &rdofs, 1);

	    double *conf;
	    double *velo;
	    double energy[10];
	    int rank;

	    ERRMEM (conf = malloc (sizeof(double) * rconf));
	    ERRMEM (velo = malloc (sizeof(double) * rdofs));

	    PBF_Double (bf, conf, rconf);
	    PBF_Double (bf, velo, rdofs);
	    PBF_Double (bf, energy, BODY_ENERGY_SIZE(rkind));
	    if (bf->parallel == PBF_ON)
	    {
	      PBF_Int (bf, &rank, 1);
	    }

	    free (conf);
	    free (velo);
	  }
	}
      }
    }
  }

  return 1;
}

/* map rigid onto FEM state */
int dom_rigid_to_fem (DOM *dom, PBF *bf, SET *subset)
{
  for (; bf; bf = bf->next)
  {
    if (PBF_Label (bf, "DOM"))
    {
      /* read iover */

      int iover = 2;

      if (PBF_Label (bf, "IOVER"))
      {
	PBF_Int (bf, &iover, 1);
      }

      ASSERT_TEXT (iover >= 3, "Output files are too old for RIGID_TO_FEM to work");

      /* read body states */

      if (subset)
      {
#if POSIX
	for (SET *item = SET_First (subset); item; item = SET_Next (item))
	{
	  regex_t xp;
	  char *pattern = item->data;
	  int error = regcomp (&xp, pattern, 0);

	  if (error != 0)
	  {
	    char *message = get_regerror (error, &xp);
	    fprintf (stderr, "-->\n");
	    fprintf (stderr, "Regular expression ERROR --> %s\n", message);
	    fprintf (stderr, "<--\n");
	    regfree (&xp);
	    free (message);
	    return 0;
	  }

	  for (BODY *bod = dom->bod; bod; bod = bod->next)
	  {
	    if (bod->label && regexec (&xp, bod->label, 0, NULL, 0) == 0)
	    {
	      if (PBF_Label (bf, bod->label))
	      {
	        double conf [12],
		       velo [6],
		       energy [4];

		int rkind;
		int rconf;
		int rdofs;

		PBF_Int (bf, &rkind, 1);
		PBF_Int (bf, &rconf, 1);
		PBF_Int (bf, &rdofs, 1);
       
		if (bod->kind == FEM && rkind == RIG)
		{

		  PBF_Double (bf, conf, 12);
		  PBF_Double (bf, velo, 6);
		  PBF_Double (bf, energy, 4);

		  BODY_From_Rigid (bod, conf, conf+9, velo, velo+3);
		}
		else
		{
		  ASSERT_TEXT (((bod->kind == RIG || bod->kind == OBS) &&
		    (rkind == RIG || rkind == OBS)) || bod->kind == (unsigned)rkind, "Body kind mismatch when reading state");
		  ASSERT_TEXT (BODY_Conf_Size (bod) == rconf, "Body configuration size mismatch when reading state");
		  ASSERT_TEXT (bod->dofs == rdofs, "Body dofs size mismatch when reading state");

		  BODY_Read_State (bod, bf, 0); /* use 0 state to skip reading of rkind, rnconf, rdofs */
		}
	      }
	    }
	  }

	  regfree (&xp);
	}
#else
	ASSERT_TEXT (0, "Regular expressions require POSIX support --> recompile Solfec with POSIX=yes");
	return 0;
#endif
      }
      else
      {
	ASSERT (PBF_Label (bf, "BODS"), ERR_FILE_FORMAT);

	int nbod;

	PBF_Int (bf, &nbod, 1);

	for (int n = 0; n < nbod; n ++)
	{
	  double conf [12], velo [6], energy [4];
	  unsigned int id;
	  BODY *bod;
	  int rank;

	  PBF_Uint (bf, &id, 1);

	  bod = MAP_Find (dom->idb, (void*) (long) id, NULL);

	  if (bod) /* update state of existing bodies only */
	  {
	    int rkind;
	    int rconf;
	    int rdofs;

	    PBF_Int (bf, &rkind, 1);
	    PBF_Int (bf, &rconf, 1);
	    PBF_Int (bf, &rdofs, 1);
   
	    if (bod->kind == FEM && rkind == RIG)
	    {

	      PBF_Double (bf, conf, 12);
	      PBF_Double (bf, velo, 6);
	      PBF_Double (bf, energy, 4);
	      if (bf->parallel == PBF_ON)
	      {
		PBF_Int (bf, &rank, 1);
	      }

	      BODY_From_Rigid (bod, conf, conf+9, velo, velo+3);
	    }
	    else
	    {
	      ASSERT_TEXT (((bod->kind == RIG || bod->kind == OBS) &&
		(rkind == RIG || rkind == OBS)) || bod->kind == (unsigned)rkind, "Body kind mismatch when reading state");
	      ASSERT_TEXT (BODY_Conf_Size (bod) == rconf, "Body configuration size mismatch when reading state");
	      ASSERT_TEXT (bod->dofs == rdofs, "Body dofs size mismatch when reading state");

	      BODY_Read_State (bod, bf, 0); /* use 0 state to skip reading of rkind, rnconf, rdofs */
	    }
	  }
	  else /* mock read */
	  {
	    int rkind;
	    int rconf;
	    int rdofs;

	    PBF_Int (bf, &rkind, 1);
	    PBF_Int (bf, &rconf, 1);
	    PBF_Int (bf, &rdofs, 1);

	    double *conf;
	    double *velo;
	    double energy[10];
	    int rank;

	    ERRMEM (conf = malloc (sizeof(double) * rconf));
	    ERRMEM (velo = malloc (sizeof(double) * rdofs));

	    PBF_Double (bf, conf, rconf);
	    PBF_Double (bf, velo, rdofs);
	    PBF_Double (bf, energy, BODY_ENERGY_SIZE(rkind));
	    if (bf->parallel == PBF_ON)
	    {
	      PBF_Int (bf, &rank, 1);
	    }

	    free (conf);
	    free (velo);
	  }
	}
      }
    }
  }

  return 1;
}
