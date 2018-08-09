// Copyright (c) 2017-2018, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory. LLNL-CODE-734707.
// All Rights reserved. See files LICENSE and NOTICE for details.
//
// This file is part of CEED, a collection of benchmarks, miniapps, software
// libraries and APIs for efficient high-order finite element and spectral
// element discretizations for exascale applications. For more information and
// source code availability see http://github.com/ceed.
//
// The CEED research is supported by the Exascale Computing Project 17-SC-20-SC,
// a collaborative effort of two U.S. Department of Energy organizations (Office
// of Science and the National Nuclear Security Administration) responsible for
// the planning and preparation of a capable exascale ecosystem, including
// software, applications, hardware, advanced system engineering and early
// testbed platforms, in support of the nation's exascale computing imperative.

#include <ceed-impl.h>
#include <string.h>
#include "ceed-opt.h"
#include <immintrin.h>

// Contracts on the middle index
// NOTRANSPOSE: V_ajc = T_jb U_abc
// TRANSPOSE:   V_ajc = T_bj U_abc
// If Add != 0, "=" is replaced by "+="
static int CeedTensorContract_Opt(Ceed ceed,
                                  CeedInt A, CeedInt B, CeedInt C, CeedInt J,
                                  const CeedScalar *restrict t,
                                  CeedTransposeMode tmode, const CeedInt Add,
                                  const CeedScalar *restrict u, CeedScalar *restrict v) {
  CeedInt tstride0 = B, tstride1 = 1;
  if (tmode == CEED_TRANSPOSE) {
    tstride0 = 1; tstride1 = J;
  }

  const int JJ = 4, CC=8;
  if (C % CC) return CeedError(ceed, 2, "Tensor [%d, %d, %d]: last dimension not divisible by %d", A, B, C, CC);
  if (J % JJ) return CeedError(ceed, 2, "Tensor [%d, %d, %d]: middle dimension output not divisible by %d", A, J, C, JJ);

  if (!Add) {
    for (CeedInt q=0; q<A*J*C; q++) {
      v[q] = (CeedScalar) 0.0;
    }
  }

  for (CeedInt a=0; a<A; a++) {
    for (CeedInt j=0; j<J; j+=JJ) {
      for (CeedInt c=0; c<C; c+=CC) {
        __m256d vv[JJ][CC/4]; // Output tile to be held in registers
        for (CeedInt jj=0; jj<JJ; jj++)
          for (CeedInt cc=0; cc<CC/4; cc++)
            vv[jj][cc] = _mm256_loadu_pd(&v[(a*J+j+jj)*C+c+cc*4]);

        for (CeedInt b=0; b<B; b++) {
          for (CeedInt jj=0; jj<JJ; jj++) { // unroll
            CeedScalar tq = t[(j+jj)*tstride0 + b*tstride1];
            for (CeedInt cc=0; cc<CC/4; cc++) { // unroll
              vv[jj][cc] += tq * _mm256_loadu_pd(&u[(a*B+b)*C+c+cc*4]);
            }
          }
        }

        for (CeedInt jj=0; jj<JJ; jj++)
          for (CeedInt cc=0; cc<CC/4; cc++)
            _mm256_storeu_pd(&v[(a*J+j+jj)*C+c+cc*4], vv[jj][cc]);

      }
    }
  }
  return 0;
}

static int CeedBasisApply_Opt(CeedBasis basis, CeedInt nelem,
                              CeedTransposeMode tmode, CeedEvalMode emode,
                              const CeedScalar *u, CeedScalar *v) {
  int ierr;
  const CeedInt dim = basis->dim;
  const CeedInt ncomp = basis->ncomp;
  const CeedInt nqpt = CeedPowInt(basis->Q1d, dim);
  const CeedInt add = (tmode == CEED_TRANSPOSE);
  const CeedInt blksize = 8;

  if ((nelem != 1) && (nelem != blksize))
    return CeedError(basis->ceed, 1,
                     "This backend does not support BasisApply for %d elements", nelem);

  if (tmode == CEED_TRANSPOSE) {
    const CeedInt vsize = nelem*ncomp*CeedPowInt(basis->P1d, dim);
    for (CeedInt i = 0; i < vsize; i++)
      v[i] = (CeedScalar) 0.0;
  }
  switch (emode) {
  case CEED_EVAL_INTERP: {
    CeedInt P = basis->P1d, Q = basis->Q1d;
    if (tmode == CEED_TRANSPOSE) {
      P = basis->Q1d; Q = basis->P1d;
    }
    CeedInt pre = ncomp*CeedPowInt(P, dim-1), post = nelem;
    CeedScalar tmp[2][nelem*ncomp*Q*CeedPowInt(P>Q?P:Q, dim-1)];
    for (CeedInt d=0; d<dim; d++) {
      ierr = CeedTensorContract_Opt(basis->ceed, pre, P, post, Q,
                                    basis->interp1d, tmode, add&&(d==dim-1),
                                    d==0?u:tmp[d%2], d==dim-1?v:tmp[(d+1)%2]);
      CeedChk(ierr);
      pre /= P;
      post *= Q;
    }
  } break;
  case CEED_EVAL_GRAD: {
    // In CEED_NOTRANSPOSE mode:
    // u is (P^dim x nc) x nelem, column-major layout (nc = ncomp)
    // v is (Q^dim x nc x dim) x nelem, column-major layout (nc = ncomp)
    // In CEED_TRANSPOSE mode, the sizes of u and v are switched.
    CeedInt P = basis->P1d, Q = basis->Q1d;
    if (tmode == CEED_TRANSPOSE) {
      P = basis->Q1d, Q = basis->Q1d;
    }
    CeedBasis_Opt *impl = basis->data;
    CeedScalar interp[nelem*ncomp*Q*CeedPowInt(P>Q?P:Q, dim-1)];
    CeedInt pre = ncomp*CeedPowInt(P, dim-1), post = nelem;
    CeedScalar tmp[2][nelem*ncomp*Q*CeedPowInt(P>Q?P:Q, dim-1)];
    // Interpolate to quadrature points (NoTranspose)
    //  or Grad to quadrature points (Transpose)
    for (CeedInt d=0; d<dim; d++) {
      ierr = CeedTensorContract_Opt(basis->ceed, pre, P, post, Q,
                                    tmode==CEED_NOTRANSPOSE
                                      ? basis->interp1d
                                      : impl->colograd1d,
                                    tmode, add&&(d>0),
                                    tmode==CEED_NOTRANSPOSE
                                      ? (d==0?u:tmp[d%2])
                                      : u + d*nqpt*ncomp*nelem,
                                    tmode==CEED_NOTRANSPOSE
                                      ? (d==dim-1?interp:tmp[(d+1)%2])
                                      : interp);
      CeedChk(ierr);
      pre /= P;
      post *= Q;
    }
    // Grad to quadrature points (NoTranspose)
    //  or Interpolate to dofs (Transpose)
    P = basis->Q1d, Q = basis->Q1d;
    if (tmode == CEED_TRANSPOSE) {
      P = basis->Q1d, Q = basis->P1d;
    }
    pre = ncomp*CeedPowInt(P, dim-1), post = nelem;
    for (CeedInt d=0; d<dim; d++) {
      ierr = CeedTensorContract_Opt(basis->ceed, pre, P, post, Q,
                                    tmode==CEED_NOTRANSPOSE
                                      ? impl->colograd1d
                                      : basis->interp1d,
                                    tmode, add&&(d==dim-1),
                                    tmode==CEED_NOTRANSPOSE
                                      ? interp
                                      : (d==0?interp:tmp[d%2]),
                                    tmode==CEED_NOTRANSPOSE
                                      ? v + d*nqpt*ncomp*nelem
                                      : (d==dim-1?v:tmp[(d+1)%2]));
      CeedChk(ierr);
      pre /= P;
      post *= Q;
    }
  } break;
  case CEED_EVAL_WEIGHT: {
    if (tmode == CEED_TRANSPOSE)
      return CeedError(basis->ceed, 1,
                       "CEED_EVAL_WEIGHT incompatible with CEED_TRANSPOSE");
    CeedInt Q = basis->Q1d;
    for (CeedInt d=0; d<dim; d++) {
      CeedInt pre = CeedPowInt(Q, dim-d-1), post = CeedPowInt(Q, d);
      for (CeedInt i=0; i<pre; i++)
        for (CeedInt j=0; j<Q; j++)
          for (CeedInt k=0; k<post; k++) {
            CeedScalar w = basis->qweight1d[j]
                           * (d == 0 ? 1 : v[((i*Q + j)*post + k)*nelem]);
            for (CeedInt e=0; e<nelem; e++)
              v[((i*Q + j)*post + k)*nelem + e] = w;
        }
    }
  } break;
  case CEED_EVAL_DIV:
    return CeedError(basis->ceed, 1, "CEED_EVAL_DIV not supported");
  case CEED_EVAL_CURL:
    return CeedError(basis->ceed, 1, "CEED_EVAL_CURL not supported");
  case CEED_EVAL_NONE:
    return CeedError(basis->ceed, 1, "CEED_EVAL_NONE does not make sense in this context");
   }
  return 0;
}

static int CeedBasisDestroy_Opt(CeedBasis basis) {
  CeedBasis_Opt *impl = basis->data;
  int ierr;

  ierr = CeedFree(&impl->colograd1d); CeedChk(ierr);
  ierr = CeedFree(&basis->data); CeedChk(ierr);

  return 0;
}

int CeedBasisCreateTensorH1_Opt(Ceed ceed, CeedInt dim, CeedInt P1d,
                                CeedInt Q1d, const CeedScalar *interp1d,
                                const CeedScalar *grad1d,
                                const CeedScalar *qref1d,
                                const CeedScalar *qweight1d,
                                CeedBasis basis) {
  CeedBasis_Opt *impl;
  int ierr;
  ierr = CeedCalloc(1,&impl); CeedChk(ierr);
  ierr = CeedMalloc(Q1d*Q1d, &impl->colograd1d); CeedChk(ierr);
  ierr = CeedBasisGetColocatedGrad(basis, impl->colograd1d); CeedChk(ierr);
  basis->data = impl;

  basis->Apply = CeedBasisApply_Opt;
  basis->Destroy = CeedBasisDestroy_Opt;
  return 0;
}
