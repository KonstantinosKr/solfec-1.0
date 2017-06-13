/*
 * mtx.h
 * Copyright (C) 2007, Tomasz Koziara (t.koziara AT gmail.com)
 * --------------------------------------------------------------
 * general sparse or dense matrix
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

#ifndef __mx__
#define __mx__

typedef struct general_matrix MX;

struct general_matrix
{
  enum {MXDENSE  = 0x01,        /* dense */
        MXBD     = 0x02,        /* block diagonal (square) */
	MXCSC    = 0x04} kind;  /* compressed columns */
  
  enum {MXTRANS  = 0x01,        /* transposed matrix (temporary) */
	MXSTATIC = 0x02,        /* static matrix */
        MXDSUBLK = 0x04,        /* diagonal sub-block (temporary) */
        MXIFAC   = 0x08,        /* factorised sparse inverse */
        MXUNINV  = 0x10,        /* on-the-fly undone sparse inverse (temporary) */
	MXSPD    = 0x20,        /* symmetric positive definite; MXCSC implies that only the lower trinagle is stored */
        MXNOTMP  = 0x40} flags; /* not temporary state enforcement flag */

  int nzmax,   /* number of nonzero entries */
          m,   /* number of rows (DENSE, BD (and columns), CSC) */
	  n,   /* number of columns (DENSE, CSC), or number of blocks (BD) */
	 *p,   /* pointers to columns (CSC), or blocks (BD); p[n] == nzmax */
	 *i,   /* indices of column row entries (CSC, i[0...nzmax-1]), or indices of the first block row/column (BD, i[n] = m) */
	 nz;   /* CSC: number of entries in triplet matrix, -1 for compressed-col; otherwise nzmax <= nz */

  double *x;   /* values, x[0...nzmax-1] */

  void *sym,   /* symbolic factorisation for CSC inverse */
       *num;   /* numeric factorisation for CSC inverse */
};

/* static dense matrix */
#define MX_DENSE(name, m, n)\
  double __##name [m*n];\
  MX name = {MXDENSE, MXSTATIC, m*n, m, n, NULL, NULL, m*n, __##name, NULL, NULL}

/* static dense matrix */
#define MX_DENSE_PTR(name, m, n, ptr)\
  MX name = {MXDENSE, MXSTATIC, m*n, m, n, NULL, NULL, m*n, ptr, NULL, NULL}

/* static block diagonal matrix */
#define MX_BD(name, nzmax, m, n, p, i)\
  double __##name [nzmax];\
  MX name = {MXBD, MXSTATIC, nzmax, m, n, p, i, nzmax, __##name, NULL, NULL}

/* static sparse matrix */
#define MX_CSC(name, nzmax, m, n, p, i)\
  double __##name [nzmax];\
  MX name = {MXCSC, MXSTATIC, nzmax, m, n, p, i, -1, __##name, NULL, NULL}

/* create a matrix => structure tables (p, i) always have
 * to be provided; the tables 'p' and 'i' are coppied */
MX* MX_Create (short kind, int m, int n, int *p, int *i);

/* create identity matrix of dimension n;
 * only MXDENSE and MXCSC kinds are valid */
MX* MX_Identity (short kind, int n);

/* set to zero */
void MX_Zero (MX *a);

/* scale */
void MX_Scale (MX *a, double b);

/* set b = a; if 'b' == NULL return
 * new matrix; otherwise return 'b' */
MX* MX_Copy (MX *a, MX *b);

/* returned = transpose (a) => IMPORTANT: never use the transposition to set a pointer;
 * it can only be used "on the fly" in order to modify an input to other routines */
MX* MX_Tran (MX *a);

/* returned = diagonal sub-block (a) => IMPORTANT: never use this function to set a pointer;
 * it can only be used "on the fly" in order to modify an input to other routines; Applies
 * only to block diagonal (BD) matrices; (from, to) correspond to blocks range (inclusive) */
MX* MX_Diag (MX *a, int from, int to);

/* returned = undone sparse inverse (a) => IMPORTANT: never use this function to set a pointer;
 * it can only be used "on the fly" in order to modify an input to other routines; Applies only to MXCSC */
MX* MX_Uninv (MX *a);

/* sum of two matrices => c = alpha * a + beta * b;
 * if 'c' == NULL return new matrix; otherwise return 'c' */
MX* MX_Add (double alpha, MX *a, double beta, MX *b, MX *c);

/* matrix matrix product => c = alpha * a * b + beta * c;
 * if 'c' == NULL return new matrix; otherwise return 'c' */
MX* MX_Matmat (double alpha, MX *a, MX *b, double beta, MX *c);

/* matrix vector product => c = alpha * a *b + beta * c */
void MX_Matvec (double alpha, MX *a, double *b, double beta, double *c);

/* inverse => b = inv (a); LU factorization is used for general CSC,
 * Cholesky for MXSPD; if 'b' == NULL return new matrix; otherwise return 'b' */
MX* MX_Inverse (MX *a, MX *b);

/* compute |n| eigenvalues & eigenvectors (vec != NULL) in the upper or
 * lower range (n < 0 or n > 0) => symmetry of 'a' is assumed and the
 * results are outputed according to the ascending order of eigenvalues */
void MX_Eigen (MX *a, int n, double *val, MX *vec);

/* For MXCSC and MXSPD matrices compute n lowest eigenvalues of the generalized
 * eigevan value problem A vec = val B vec, where A is symmetric semi-positive definite,
 * and B is diagonal positive definite, abstol is the absolut tolerance of eigenvalues
 * computation, and maxiter is the iterations bound; returns the number of iterations or -1 on failure */
int MX_CSC_Geneigen (MX *A, MX *B, int n, double abstol, int maxiter, int verbose, double *val, MX *vec);

/* compure 2-norm */
double MX_Norm (MX *a);

/* pack/unpack matrix into/from integer and double storage */
void MX_Pack (MX *a, int *dsize, double **d, int *doubles, int *isize, int **i, int *ints);
MX* MX_Unpack (int *dpos, double *d, int doubles, int *ipos, int *i, int ints);

/* print to standard output */
void MX_Printf (MX *a);

/* print into a MatrixMarket file */
void MX_MatrixMarket (MX *a, const char *path);

/* free matrix */
void MX_Destroy (MX *a);

#endif
