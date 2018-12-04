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
#include "../include/ceed.h"
#include "ceed-cuda.h"

//*********************
// Ref kernels

static const char *basiskernels = QUOTE(
extern "C" __global__ void interp(const CeedInt nelem, const int transpose, const CeedScalar * __restrict__ interp1d, const CeedScalar * __restrict__ u, CeedScalar *__restrict__ v) {
  const CeedInt i = threadIdx.x;

  __shared__ CeedScalar s_mem[Q1D * P1D + 2 * BASIS_BUF_LEN];
  CeedScalar *s_interp1d = s_mem;
  CeedScalar *s_buf1 = s_mem + Q1D * P1D;
  CeedScalar *s_buf2 = s_buf1 + BASIS_BUF_LEN;
  for (CeedInt k = i; k < Q1D * P1D; k += blockDim.x) {
    s_interp1d[k] = interp1d[k];
  }

  const CeedInt P = transpose ? Q1D : P1D;
  const CeedInt Q = transpose ? P1D : Q1D;
  const CeedInt stride0 = transpose ? 1 : P1D;
  const CeedInt stride1 = transpose ? P1D : 1;
  const CeedInt u_stride = BASIS_NCOMP * (transpose ? BASIS_NQPT : BASIS_ELEMSIZE);
  const CeedInt v_stride = BASIS_NCOMP * (transpose ? BASIS_ELEMSIZE : BASIS_NQPT);

  for (CeedInt elem = blockIdx.x; elem < nelem; elem += gridDim.x) {
    const CeedScalar *cur_u = u + elem * u_stride;
    CeedScalar *cur_v = v + elem * v_stride;
    for (CeedInt k = i; k < u_stride; k += blockDim.x) {
      s_buf1[k] = cur_u[k];
    }

    CeedInt pre = u_stride;
    CeedInt post = 1;
    for (CeedInt d = 0; d < BASIS_DIM; d++) {
      __syncthreads();

      pre /= P;
      const CeedScalar *in = d % 2 ? s_buf2 : s_buf1;
      CeedScalar *out = d == BASIS_DIM - 1 ? cur_v : (d % 2 ? s_buf1 : s_buf2);

      const CeedInt writeLen = pre * post * Q;
      for (CeedInt k = i; k < writeLen; k += blockDim.x) {
        const CeedInt c = k % post;
        const CeedInt j = (k / post) % Q;
        const CeedInt a = k / (post * Q);
        CeedScalar vk = 0;
        for (CeedInt b = 0; b < P; b++) {
          vk += s_interp1d[j * stride0 + b * stride1] * in[(a * P + b) * post + c];
        }

        out[k] = vk;
      }

      post *= Q;
    }
  }
}

extern "C" __global__ void grad(const CeedInt nelem, const int transpose, const CeedScalar * __restrict__ interp1d, const CeedScalar * __restrict__ grad1d, const CeedScalar * __restrict__ u, CeedScalar *__restrict__ v) {
  const CeedInt i = threadIdx.x;

  __shared__ CeedScalar s_mem[2 * (Q1D * P1D + BASIS_BUF_LEN)];
  CeedScalar *s_interp1d = s_mem;
  CeedScalar *s_grad1d = s_interp1d + Q1D * P1D;
  CeedScalar *s_buf1 = s_grad1d + Q1D * P1D;
  CeedScalar *s_buf2 = s_buf1 + BASIS_BUF_LEN;
  for (CeedInt k = i; k < Q1D * P1D; k += blockDim.x) {
    s_interp1d[k] = interp1d[k];
    s_grad1d[k] = grad1d[k];
  }


  const CeedInt P = transpose ? Q1D : P1D;
  const CeedInt Q = transpose ? P1D : Q1D;
  const CeedInt stride0 = transpose ? 1 : P1D;
  const CeedInt stride1 = transpose ? P1D : 1;
  const CeedInt u_stride = BASIS_NCOMP * (transpose ? BASIS_NQPT * BASIS_DIM : BASIS_ELEMSIZE);
  const CeedInt v_stride = BASIS_NCOMP * (transpose ? BASIS_ELEMSIZE : BASIS_NQPT * BASIS_DIM);

  for (CeedInt elem = blockIdx.x; elem < nelem; elem += gridDim.x) {
    const CeedScalar *cur_u = u + elem * u_stride;
    CeedScalar *cur_v = v + elem * v_stride;

    for (CeedInt dim1 = 0; dim1 < BASIS_DIM; dim1++) {
      CeedInt pre = BASIS_NCOMP * (transpose ? BASIS_NQPT : BASIS_ELEMSIZE);
      CeedInt post = 1;
      for (CeedInt dim2 = 0; dim2 < BASIS_DIM; dim2++) {
        __syncthreads();

        pre /= P;
        const CeedScalar *op = dim1 == dim2 ? s_grad1d : s_interp1d;
        const CeedScalar *in = dim2 == 0 ? cur_u : (dim2 % 2 ? s_buf2 : s_buf1);
        CeedScalar *out = dim2 == BASIS_DIM - 1 ? cur_v : (dim2 % 2 ? s_buf1 : s_buf2);

        const CeedInt writeLen = pre * post * Q;
        for (CeedInt k = i; k < writeLen; k += blockDim.x) {
          const CeedInt c = k % post;
          const CeedInt j = (k / post) % Q;
          const CeedInt a = k / (post * Q);
          CeedScalar vk = 0;
          for (CeedInt b = 0; b < P; b++) {
            vk += op[j * stride0 + b * stride1] * in[(a * P + b) * post + c];
          }

          if (transpose && dim2 == BASIS_DIM - 1)
            out[k] += vk;
          else
            out[k] = vk;
        }

        post *= Q;
      }
      if (transpose) {
        cur_u += BASIS_NQPT * BASIS_NCOMP;
      } else {
        cur_v += BASIS_NQPT * BASIS_NCOMP;
      }
    }
  }
}

extern "C" __global__ void weight(const CeedScalar * __restrict__ qweight1d, CeedScalar * __restrict__ v) {
  CeedInt pre = BASIS_NQPT;
  CeedInt post = 1;
  for (CeedInt d=0; d<BASIS_DIM; d++) {
    pre /= Q1D;
    for (CeedInt i=0; i<pre; i++) {
      for (CeedInt j=0; j<Q1D; j++) {
        for (CeedInt k=0; k<post; k++) {
          v[(i*Q1D + j)*post + k] = qweight1d[j] * (d == 0 ? 1 : v[(i*Q1D + j)*post + k]);
        }
      }
    }
    post *= Q1D;
  }
}
);

//*********************
// 3dreg kernels
static const char *kernels3dreg = QUOTE(

typedef double real;

inline __device__ void Contract(const real *A, const real *B,
                                 int nA1, int nA2, int nA3,
                                 int nB1, int nB2, real *T)
{
#pragma unroll
    for (int l = 0; l < nA2*nA3*nB2; l++) T[l] = 0.0;
#pragma unroll
    for (int a2 = 0; a2 < nA2; a2++)
#pragma unroll
        for (int a3 = 0; a3 < nA3; a3++)
#pragma unroll
            for (int b2 = 0; b2 < nB2; b2++)
#pragma unroll
                for (int t = 0; t < nB1; t++)
                {
                    T[a2 + a3*nA2 + b2*nA2*nA3] += B[b2*nB1 + t] * A[a3*nA2*nA1 + a2*nA1 + t];
                }
}

inline __device__ void ContractTranspose(const real *A, const real *B,
                                         int nA1, int nA2, int nA3,
                                         int nB1, int nB2, real *T)
{
#pragma unroll
    for (int l = 0; l < nA2*nA3*nB2; l++) T[l] = 0.0;
#pragma unroll
    for (int a2 = 0; a2 < nA2; a2++)
#pragma unroll
        for (int a3 = 0; a3 < nA3; a3++)
#pragma unroll
            for (int b1 = 0; b1 < nB1; b1++)
#pragma unroll
                for (int t = 0; t < nB2; t++)
                {
                    T[a2 + a3*nA2 + b1*nA2*nA3] += B[t*nB1 + b1] * A[a3*nA2*nA1 + a2*nA1 + t];
                }
}

extern "C" __global__ void interp(const CeedInt nelem, const int transpose, const CeedScalar *c_B, const CeedScalar * __restrict__ d_U, CeedScalar *__restrict__ d_V)
{
    real r_V[Q1D*Q1D*Q1D];
    real r_t[Q1D*Q1D*Q1D];

    const int tid = threadIdx.x;
    const int bid = blockIdx.x;

  if(bid<nelem){
#pragma unroll
    for (int i = 0; i < P1D*P1D*P1D; i++)
      r_V[i] = d_U[bid*32*P1D*P1D*P1D + 32*i + tid];

    if(!transpose){
      Contract(r_V, c_B, P1D, P1D, P1D, P1D, Q1D, r_t);
      Contract(r_t, c_B, P1D, P1D, Q1D, P1D, Q1D, r_V); 
      Contract(r_V, c_B, P1D, Q1D, Q1D, P1D, Q1D, r_t);
    } else {
      ContractTranspose(r_V, c_B, Q1D, Q1D, Q1D, P1D, Q1D, r_t);
      ContractTranspose(r_t, c_B, Q1D, Q1D, P1D, P1D, Q1D, r_V);
      ContractTranspose(r_V, c_B, Q1D, P1D, P1D, P1D, Q1D, r_t);
    }
    
#pragma unroll 
    for (int i = 0; i < P1D*P1D*P1D; i++) d_V[bid*32*P1D*P1D*P1D + i*32 + tid] = r_t[i];
  }
}   

);

int CeedBasisApply_Cuda(CeedBasis basis, const CeedInt nelem, CeedTransposeMode tmode,
    CeedEvalMode emode, CeedVector u, CeedVector v) {
  int ierr;
  Ceed ceed;
  ierr = CeedBasisGetCeed(basis, &ceed); CeedChk(ierr);
  Ceed_Cuda* ceed_Cuda;
  CeedGetData(ceed, (void*) &ceed_Cuda); CeedChk(ierr);
  CeedBasis_Cuda *data;
  CeedBasisGetData(basis, (void*)&data); CeedChk(ierr);
  const CeedInt transpose = tmode == CEED_TRANSPOSE;
  const int blocksize = ceed_Cuda->optblocksize;

  const CeedScalar *d_u;
  CeedScalar *d_v;
  if(emode!=CEED_EVAL_WEIGHT){
    ierr = CeedVectorGetArrayRead(u, CEED_MEM_DEVICE, &d_u); CeedChk(ierr);
  }
  ierr = CeedVectorGetArray(v, CEED_MEM_DEVICE, &d_v); CeedChk(ierr);

  if (tmode == CEED_TRANSPOSE) {
    ierr = cudaMemset(d_v, 0, v->length * sizeof(CeedScalar)); CeedChk(ierr);
  }
  if (emode == CEED_EVAL_INTERP) {
    void *interpargs[] = {(void*)&nelem, (void*)&transpose, &data->d_interp1d, &d_u, &d_v};
    ierr = run_kernel(ceed, data->interp, nelem, blocksize, interpargs); CeedChk(ierr);
  } else if (emode == CEED_EVAL_GRAD) {
    void *gradargs[] = {(void*)&nelem, (void*)&transpose, &data->d_interp1d, &data->d_grad1d, &d_u, &d_v};
    ierr = run_kernel(ceed, data->grad, nelem, blocksize, gradargs); CeedChk(ierr);
  } else if (emode == CEED_EVAL_WEIGHT) {
    void *weightargs[] = {&data->d_qweight1d, &d_v};
    ierr = run_kernel(ceed, data->weight, 1, 1, weightargs); CeedChk(ierr);
  }

  if(emode!=CEED_EVAL_WEIGHT){
    ierr = CeedVectorRestoreArrayRead(u, &d_u); CeedChk(ierr);
  }
  ierr = CeedVectorRestoreArray(v, &d_v); CeedChk(ierr);

  return 0;
}

int initInterp(CeedScalar* d_B, CeedInt P1d, CeedInt Q1d, CeedScalar** c_B);

int CeedBasisApply_Cuda_3dreg(CeedBasis basis, const CeedInt nelem, CeedTransposeMode tmode,
    CeedEvalMode emode, CeedVector u, CeedVector v) {
  int ierr;
  Ceed ceed;
  ierr = CeedBasisGetCeed(basis, &ceed); CeedChk(ierr);
  Ceed_Cuda* ceed_Cuda;
  CeedGetData(ceed, (void*) &ceed_Cuda); CeedChk(ierr);
  CeedBasis_Cuda *data;
  CeedBasisGetData(basis, (void*)&data); CeedChk(ierr);
  const CeedInt transpose = tmode == CEED_TRANSPOSE;
  const int warpsize  = 32;
  const int blocksize = warpsize;
  const int gridsize  = nelem/warpsize + ( (nelem/warpsize*warpsize<nelem)? 1 : 0 );

  const CeedScalar *d_u;
  CeedScalar *d_v;
  if(emode!=CEED_EVAL_WEIGHT){
    ierr = CeedVectorGetArrayRead(u, CEED_MEM_DEVICE, &d_u); CeedChk(ierr);
  }
  ierr = CeedVectorGetArray(v, CEED_MEM_DEVICE, &d_v); CeedChk(ierr);

  if (tmode == CEED_TRANSPOSE) {
    ierr = cudaMemset(d_v, 0, v->length * sizeof(CeedScalar)); CeedChk(ierr);
  }
  if (emode == CEED_EVAL_INTERP) {
    CeedScalar* c_B;
    ierr = initInterp(data->d_interp1d, basis->P1d, basis->Q1d, &c_B); CeedChk(ierr);
    void *interpargs[] = {(void*)&nelem, (void*)&transpose, &c_B, &d_u, &d_v};
    //void *interpargs[] = {(void*)&nelem, (void*)&transpose, &data->d_interp1d, &d_u, &d_v}; 
    ierr = run_kernel(ceed, data->interp, gridsize, blocksize, interpargs); CeedChk(ierr);
  } else if (emode == CEED_EVAL_GRAD) {
    // void *gradargs[] = {(void*)&nelem, (void*)&transpose, &data->d_interp1d, &data->d_grad1d, &d_u, &d_v};
    // ierr = run_kernel(ceed, data->grad, nelem, blocksize, gradargs); CeedChk(ierr);
  } else if (emode == CEED_EVAL_WEIGHT) {
    // void *weightargs[] = {&data->d_qweight1d, &d_v};
    // ierr = run_kernel(ceed, data->weight, 1, 1, weightargs); CeedChk(ierr);
  }

  if(emode!=CEED_EVAL_WEIGHT){
    ierr = CeedVectorRestoreArrayRead(u, &d_u); CeedChk(ierr);
  }
  ierr = CeedVectorRestoreArray(v, &d_v); CeedChk(ierr);

  return 0;
}

static int CeedBasisDestroy_Cuda(CeedBasis basis) {
  int ierr;

  CeedBasis_Cuda *data;
  ierr = CeedBasisGetData(basis, (void*) &data); CeedChk(ierr);

  CeedChk_Cu(basis->ceed, cuModuleUnload(data->module)); 

  ierr = cudaFree(data->d_qweight1d); CeedChk(ierr);
  ierr = cudaFree(data->d_interp1d); CeedChk(ierr);
  ierr = cudaFree(data->d_grad1d); CeedChk(ierr);

  ierr = CeedFree(&data); CeedChk(ierr);

  return 0;
}

int CeedBasisCreateTensorH1_Cuda(CeedInt dim, CeedInt P1d, CeedInt Q1d,
    const CeedScalar *interp1d,
    const CeedScalar *grad1d,
    const CeedScalar *qref1d,
    const CeedScalar *qweight1d,
    CeedBasis basis) {
  int ierr;
  CeedBasis_Cuda *data;
  ierr = CeedCalloc(1, &data); CeedChk(ierr);
  
  const CeedInt qBytes = basis->Q1d * sizeof(CeedScalar);
  ierr = cudaMalloc((void**)&data->d_qweight1d, qBytes); CeedChk(ierr);
  ierr = cudaMemcpy(data->d_qweight1d, basis->qweight1d, qBytes, cudaMemcpyHostToDevice); CeedChk(ierr);

  const CeedInt iBytes = qBytes * basis->P1d;
  ierr = cudaMalloc((void**)&data->d_interp1d, iBytes); CeedChk(ierr);
  ierr = cudaMemcpy(data->d_interp1d, basis->interp1d, iBytes, cudaMemcpyHostToDevice); CeedChk(ierr);

  ierr = cudaMalloc((void**)&data->d_grad1d, iBytes); CeedChk(ierr);
  ierr = cudaMemcpy(data->d_grad1d, basis->grad1d, iBytes, cudaMemcpyHostToDevice); CeedChk(ierr);

  ierr = compile(basis->ceed, kernels3dreg, &data->module, 7,
      "Q1D", basis->Q1d,
      "P1D", basis->P1d,
      "BASIS_BUF_LEN", basis->ncomp * CeedIntPow(basis->Q1d > basis->P1d ? basis->Q1d : basis->P1d, basis->dim),
      "BASIS_DIM", basis->dim,
      "BASIS_NCOMP", basis->ncomp,
      "BASIS_ELEMSIZE", CeedIntPow(basis->P1d, basis->dim),
      "BASIS_NQPT", CeedIntPow(basis->Q1d, basis->dim)
      ); CeedChk(ierr);
  ierr = get_kernel(basis->ceed, data->module, "interp", &data->interp); CeedChk(ierr);
  // ierr = get_kernel(basis->ceed, data->module, "grad", &data->grad); CeedChk(ierr);
  // ierr = get_kernel(basis->ceed, data->module, "weight", &data->weight); CeedChk(ierr);

  Ceed ceed;
  ierr = CeedBasisGetCeed(basis, &ceed); CeedChk(ierr);
  ierr = CeedBasisSetData(basis, (void*)&data);
  CeedChk(ierr);
  ierr = CeedSetBackendFunction(ceed, "Basis", basis, "Apply", CeedBasisApply_Cuda);
  CeedChk(ierr);
  ierr = CeedSetBackendFunction(ceed, "Basis", basis, "Destroy", CeedBasisDestroy_Cuda);
  CeedChk(ierr);
  return 0;
}

int CeedBasisCreateH1_Cuda(CeedElemTopology topo, CeedInt dim,
                          CeedInt ndof, CeedInt nqpts,
                          const CeedScalar *interp,
                          const CeedScalar *grad,
                          const CeedScalar *qref,
                          const CeedScalar *qweight,
                          CeedBasis basis) {
  int ierr;
  Ceed ceed;
  ierr = CeedBasisGetCeed(basis, &ceed); CeedChk(ierr);
  return CeedError(ceed, 1, "Backend does not implement generic H1 basis");
}
