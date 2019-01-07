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

// *****************************************************************************
extern "C" __global__ void setup(void *ctx, CeedInt Q, Fields_Cuda fields) {
  const CeedScalar *w = fields.inputs[0];
  CeedScalar *qdata = fields.outputs[0];
  for (int i = blockIdx.x * blockDim.x + threadIdx.x;
    i < Q;
    i += blockDim.x * gridDim.x)
  {
    qdata[i] = w[i];
  }
}

// *****************************************************************************
extern "C" __global__ void mass(void *ctx, CeedInt Q, Fields_Cuda fields) {
  CeedScalar *scale = (CeedScalar *)ctx;
  CeedScalar val = scale[4];
  const CeedScalar *qdata = fields.inputs[0], *u = fields.inputs[1];
  CeedScalar *v = fields.outputs[0];
  for (int i = blockIdx.x * blockDim.x + threadIdx.x;
    i < Q;
    i += blockDim.x * gridDim.x)
  {
    v[i] = val * qdata[i] * u[i];
  }
}

// // *****************************************************************************
// extern "C" __global__ void setup(void *ctx, CeedInt Q, const CeedScalar *const *in,
//                       CeedScalar *const *out) {
//   const CeedScalar *w = in[0];
//   CeedScalar *qdata = out[0];
//   for (int i = blockIdx.x * blockDim.x + threadIdx.x;
//     i < Q;
//     i += blockDim.x * gridDim.x)
//   {
//     qdata[i] = w[i];
//   }
// }

// // *****************************************************************************
// extern "C" __global__ void mass(void *ctx, CeedInt Q, const CeedScalar *const *in,
//                      CeedScalar *const *out) {
//   const CeedScalar *qdata = in[0], *u = in[1];
//   CeedScalar *v = out[0];
//   for (int i = blockIdx.x * blockDim.x + threadIdx.x;
//     i < Q;
//     i += blockDim.x * gridDim.x)
//   {
//     v[i] = qdata[i] * u[i];
//   }
// }
