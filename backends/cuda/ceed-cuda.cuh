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

#include <algorithm>
#include <ceed-impl.h>
#include <stdbool.h>

#define gpuErrchk(ans) { gpuAssert((ans), __FILE__, __LINE__); }
static inline void gpuAssert(cudaError_t code, const char *file, int line, bool abort=true)
{
  fprintf(stderr,"GPUassert %d: %s %s %d\n", code, cudaGetErrorString(code), file, line);
  if (code != cudaSuccess) exit(code);
}



typedef struct {
  CeedScalar *h_array;
  CeedScalar *h_array_allocated;
  CeedScalar *d_array;
  CeedScalar *d_array_allocated;
} CeedVector_Cuda;

typedef struct {
  CeedInt *d_indices;
} CeedElemRestriction_Cuda;

typedef struct {
  bool ready;
  CeedVector etmp;
  CeedVector qdata;
  CeedVector BEu, BEv;
} CeedOperator_Cuda;

typedef struct {
  bool ready;
  int nc, dim, nelem;
  void *d_c;
  int *d_ierr;
} CeedQFunction_Cuda;

typedef struct {
  bool ready;
  CeedScalar *d_qweight1d;
  CeedScalar *d_interp1d;
  CeedScalar *d_grad1d;
  CeedScalar *d_tmp1;
  CeedScalar *d_tmp2;
} CeedBasis_Cuda;

typedef struct {
  int deviceID;
  int optBlockSize;
  int xThreadLimit, yThreadLimit, zThreadLimit;
} Ceed_Cuda;

static int divup(int a, int b) {
  return (a + b - 1) / b;
}

template <typename CudaFunc, typename... Args>
int run1d(const Ceed_Cuda* ceed, CudaFunc f, const CeedInt sharedMem, const CeedInt a, Args... args) {
  int optThreads = 0;
  int optBlocks = 0;
  cudaOccupancyMaxPotentialBlockSize(&optBlocks, &optThreads, f, sharedMem, 0);

  const int threads = std::min({a, optThreads, ceed->xThreadLimit});
  const int blocks = divup(a, threads);
  f<<<blocks, threads, sharedMem>>>(a, args...);
  return cudaGetLastError();
}

template <typename CudaFunc, typename... Args>
int run2d(const Ceed_Cuda* ceed, CudaFunc f, const CeedInt sharedMem, const CeedInt a, const CeedInt b, Args... args) {
  int optThreads = 0;
  int optBlocks = 0;
  cudaOccupancyMaxPotentialBlockSize(&optBlocks, &optThreads, f, sharedMem, 0);

  const int threadsX = std::min({b, optThreads, ceed->xThreadLimit});
  const int threadsY = std::min({a, optThreads / threadsX, ceed->yThreadLimit});

  const int blocksX = divup(b, threadsX);
  const int blocksY = divup(a, threadsY);

  f<<<dim3(blocksX, blocksY), dim3(threadsX, threadsY), sharedMem>>>(a, b, args...);
  return cudaGetLastError();
}

template <typename CudaFunc, typename... Args>
int run3d(const Ceed_Cuda* ceed, CudaFunc f, const CeedInt sharedMem, const CeedInt a, const CeedInt b, const CeedInt c, Args... args) {
  int optThreads = 0;
  int optBlocks = 0;
  cudaOccupancyMaxPotentialBlockSize(&optBlocks, &optThreads, f, sharedMem, 0);

  const int threadsX = std::min({c, optThreads, ceed->xThreadLimit});
  const int threadsY = std::min({b, optThreads / threadsX, ceed->yThreadLimit});
  const int threadsZ = std::min({a, optThreads / (threadsX * threadsY), ceed->zThreadLimit});

  const int blocksX = divup(c, threadsX);
  const int blocksY = divup(b, threadsY);
  const int blocksZ = divup(a, threadsZ);

  f<<<dim3(blocksX, blocksY, blocksZ), dim3(threadsX, threadsY, threadsZ), sharedMem>>>(a, b, c, args...);
  return cudaGetLastError();
}

CEED_INTERN int CeedVectorCreate_Cuda(Ceed ceed, CeedInt n, CeedVector vec);

CEED_INTERN int CeedElemRestrictionCreate_Cuda(CeedElemRestriction r,
    CeedMemType mtype,
    CeedCopyMode cmode, const CeedInt *indices);

CEED_INTERN int CeedBasisApplyElems_Cuda(CeedBasis basis, const CeedInt nelem,
    CeedTransposeMode tmode, CeedEvalMode emode, const CeedVector u, CeedVector v);

CEED_INTERN int CeedQFunctionApplyElems_Cuda(CeedQFunction qf, CeedVector qdata, const CeedInt Q, const CeedInt nelem,
    const CeedVector u, CeedVector v);

CEED_INTERN int CeedBasisCreateTensorH1_Cuda(Ceed ceed, CeedInt dim, CeedInt P1d,
    CeedInt Q1d, const CeedScalar *interp1d,
    const CeedScalar *grad1d,
    const CeedScalar *qref1d,
    const CeedScalar *qweight1d,
    CeedBasis basis);

CEED_INTERN int CeedQFunctionCreate_Cuda(CeedQFunction qf);

CEED_INTERN int CeedOperatorCreate_Cuda(CeedOperator op);