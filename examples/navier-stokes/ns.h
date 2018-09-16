// Copyright (c) 2017, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-734707. All Rights
// reserved. See files LICENSE and NOTICE for details.
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

#include <math.h>

// *****************************************************************************
static int Setup(void *ctx, CeedInt Q,
                 const CeedScalar *const *in, CeedScalar *const *out) {
  // Inputs
  const CeedScalar *x = in[0], *J = in[1], *w = in[2];
  // Outputs
  CeedScalar *qdata = out[0], *q0 = out[1];
  // Context
  const CeedScalar *context = (const CeedScalar*)ctx;
  const CeedScalar Rd         = context[0];
  const CeedScalar Ts         = context[1];
  const CeedScalar p0         = context[2];
  const CeedScalar cv         = context[3];
  const CeedScalar cp         = context[4];
  const CeedScalar g          = context[5];

  // Quadrature Point Loop
  for (CeedInt i=0; i<Q; i++) {
    // Setup
    const CeedScalar J11 = J[i+Q*0];
    const CeedScalar J21 = J[i+Q*1];
    const CeedScalar J31 = J[i+Q*2];
    const CeedScalar J12 = J[i+Q*3];
    const CeedScalar J22 = J[i+Q*4];
    const CeedScalar J32 = J[i+Q*5];
    const CeedScalar J13 = J[i+Q*6];
    const CeedScalar J23 = J[i+Q*7];
    const CeedScalar J33 = J[i+Q*8];
    const CeedScalar A11 = J22*J33 - J23*J32;
    const CeedScalar A12 = J13*J32 - J12*J33;
    const CeedScalar A13 = J12*J23 - J13*J22;
    const CeedScalar A21 = J23*J31 - J21*J33;
    const CeedScalar A22 = J11*J33 - J13*J31;
    const CeedScalar A23 = J13*J21 - J11*J23;
    const CeedScalar A31 = J21*J32 - J22*J31;
    const CeedScalar A32 = J12*J31 - J11*J32;
    const CeedScalar A33 = J11*J22 - J12*J21;
    const CeedScalar qw = w[i] / (J11*A11 + J21*A12 + J31*A13);

    // Qdata
    // -- Interp-to-Interp qdata
    qdata[i+ 0*Q] = w[i] * (J11*A11 + J21*A12 + J31*A13);
    // -- Interp-to-Grad qdata
    qdata[i+ 1*Q] = w[i] * A11;
    qdata[i+ 2*Q] = w[i] * A21;
    qdata[i+ 3*Q] = w[i] * A31;
    qdata[i+ 4*Q] = w[i] * A12;
    qdata[i+ 5*Q] = w[i] * A22;
    qdata[i+ 6*Q] = w[i] * A32;
    qdata[i+ 7*Q] = w[i] * A13;
    qdata[i+ 8*Q] = w[i] * A23;
    qdata[i+ 9*Q] = w[i] * A33;
    // -- Grad-to-Grad qdata
    qdata[i+10*Q] = qw * (A11*A11 + A12*A12 + A13*A13);
    qdata[i+11*Q] = qw * (A11*A21 + A12*A22 + A13*A23);
    qdata[i+12*Q] = qw * (A11*A31 + A12*A32 + A13*A33);
    qdata[i+13*Q] = qw * (A21*A21 + A22*A22 + A23*A23);
    qdata[i+14*Q] = qw * (A21*A31 + A22*A32 + A23*A33);
    qdata[i+15*Q] = qw * (A31*A31 + A32*A32 + A33*A33);

    // Initial Conditions
    CeedScalar T = Ts - g*x[i+Q*2]/cv;
    CeedScalar p = p0 * pow(T/Ts, Rd/cp);
    CeedScalar rho = p / (Rd*T);
    q0[i+0*Q] = rho;
    q0[i+1*Q] = 0.0;
    q0[i+2*Q] = 0.0;
    q0[i+3*Q] = 0.0;
    q0[i+4*Q] = rho*cv*T + rho*g*x[i+Q*2];

  } // End of Quadrature Point Loop

  // Return
  return 0;
}

static int NS(void *ctx, CeedInt Q,
                const CeedScalar *const *in, CeedScalar *const *out) {
  // Inputs
  const CeedScalar *q = in[0], *dq = in[1], *qdata = in[2], *x = in[3];
  // Outputs
  CeedScalar *v = out[0], *vg = out[1];
  // Context
  const CeedScalar *context = (const CeedScalar*)ctx;
  const CeedScalar lambda     = context[0];
  const CeedScalar mu         = context[1];
  const CeedScalar Pr         = context[2];
  const CeedScalar cp         = context[3];
  const CeedScalar cv         = context[4];
  const CeedScalar g          = context[5];
  const CeedScalar gamma      = cp / cv;

  // Quadrature Point Loop
  for (CeedInt i=0; i<Q; i++) {
    // Setup
    // -- Interp in
    const CeedScalar rho     =   q[i+0*Q];
    const CeedScalar u[3]    = { q[i+1*Q] / rho,
                                 q[i+2*Q] / rho,
                                 q[i+3*Q] / rho };
    const CeedScalar E       =   q[i+4*Q];
    // -- Grad in
    const CeedScalar drho[3] = {  dq[i+(0+5*0)*Q],
                                  dq[i+(0+5*1)*Q],
                                  dq[i+(0+5*2)*Q] };
    const CeedScalar du[9]   = { (dq[i+(1+5*0)*Q] - drho[0]*u[0]) / rho,
                                 (dq[i+(1+5*1)*Q] - drho[1]*u[0]) / rho,
                                 (dq[i+(1+5*2)*Q] - drho[2]*u[0]) / rho,
                                 (dq[i+(2+5*0)*Q] - drho[0]*u[1]) / rho,
                                 (dq[i+(2+5*1)*Q] - drho[1]*u[1]) / rho,
                                 (dq[i+(2+5*2)*Q] - drho[2]*u[1]) / rho,
                                 (dq[i+(3+5*0)*Q] - drho[0]*u[2]) / rho,
                                 (dq[i+(3+5*1)*Q] - drho[1]*u[2]) / rho,
                                 (dq[i+(3+5*2)*Q] - drho[2]*u[2]) / rho };
    const CeedScalar dE[3]   = {  dq[i+(4+5*0)*Q],
                                  dq[i+(4+5*1)*Q],
                                  dq[i+(4+5*2)*Q] };
    // -- Interp-to-Interp qdata
    const CeedScalar J       =   qdata[i+ 0*Q];
    // -- Interp-to-Grad qdata
    const CeedScalar BJ[9]   = { qdata[i+ 1*Q],
                                 qdata[i+ 2*Q],
                                 qdata[i+ 3*Q],
                                 qdata[i+ 4*Q],
                                 qdata[i+ 5*Q],
                                 qdata[i+ 6*Q],
                                 qdata[i+ 7*Q],
                                 qdata[i+ 8*Q],
                                 qdata[i+ 9*Q] };
    // -- Grad-to-Grad qdata
    const CeedScalar BBJ[6]  = { qdata[i+10*Q],
                                 qdata[i+11*Q],
                                 qdata[i+12*Q],
                                 qdata[i+13*Q],
                                 qdata[i+14*Q],
                                 qdata[i+15*Q] };
    // -- gradT
    const CeedScalar gradT[3] = { (dE[0]/rho - E*drho[0]/(rho*rho) -
                                    u[0]*du[0+3*0]) / cv,
                                  (dE[1]/rho - E*drho[1]/(rho*rho) -
                                    u[1]*du[1+3*1]) / cv,
                                  (dE[2]/rho - E*drho[2]/(rho*rho) -
                                    u[2]*du[2+3*2] - g) / cv };
    // -- Fuvisc
    const CeedScalar Fu[6] =  { mu * (du[0+3*0] * (2 + lambda)),
                                mu * (du[0+3*1] + du[1+3*0]),
                                mu * (du[0+3*2] + du[2+3*0]),
                                mu * (du[1+3*1] * (2 + lambda)),
                                mu * (du[1+3*2] + du[2+3*1]),
                                mu * (du[2+3*2] * (2 + lambda)) };

    // -- Fevisc
    const CeedScalar Fe[3] = { u[0]*Fu[0] + u[1]*Fu[1] + u[2]*Fu[2] +
                                 (mu*cp/Pr) * gradT[0],
                               u[0]*Fu[1] + u[1]*Fu[3] + u[2]*Fu[4] +
                                 (mu*cp/Pr) * gradT[1],
                               u[0]*Fu[2] + u[1]*Fu[4] + u[2]*Fu[5] +
                                 (mu*cp/Pr) * gradT[2] };
    // -- P
    const CeedScalar P = (E - (u[0]*u[0] + u[1]*u[1] + u[2]*u[2])*rho/2 -
                               rho*g*x[i+Q*2]) * (gamma - 1);


    // The Physics

    // -- Density
    // ---- u rho
    vg[i+(0+5*0)*Q] = rho*u[0]*BJ[0] + rho*u[1]*BJ[1] + rho*u[1]*BJ[2];
    vg[i+(0+5*1)*Q] = rho*u[0]*BJ[3] + rho*u[1]*BJ[4] + rho*u[1]*BJ[5];
    vg[i+(0+5*2)*Q] = rho*u[0]*BJ[6] + rho*u[1]*BJ[7] + rho*u[1]*BJ[8];

    // -- Momentum
    // ---- rho (u x u) + P I3
    vg[i+(1+5*0)*Q]  = (rho*u[0]*u[0]+P)*BJ[0] + rho*u[0]*u[1]*BJ[1] +
                         rho*u[0]*u[2]*BJ[2];
    vg[i+(1+5*1)*Q]  = (rho*u[0]*u[0]+P)*BJ[3] + rho*u[0]*u[1]*BJ[4] +
                         rho*u[0]*u[2]*BJ[5];
    vg[i+(1+5*2)*Q]  = (rho*u[0]*u[0]+P)*BJ[6] + rho*u[0]*u[1]*BJ[7] +
                         rho*u[0]*u[2]*BJ[8];
    vg[i+(2+5*0)*Q]  =  rho*u[1]*u[0]*BJ[0] + (rho*u[1]*u[1]+P)*BJ[1] +
                         rho*u[1]*u[2]*BJ[2];
    vg[i+(2+5*1)*Q]  =  rho*u[1]*u[0]*BJ[3] + (rho*u[1]*u[1]+P)*BJ[4] +
                         rho*u[1]*u[2]*BJ[5];
    vg[i+(2+5*2)*Q]  =  rho*u[1]*u[0]*BJ[6] + (rho*u[1]*u[1]+P)*BJ[7] +
                         rho*u[1]*u[2]*BJ[8];
    vg[i+(3+5*0)*Q]  =  rho*u[2]*u[0]*BJ[0] + rho*u[2]*u[1]*BJ[1] +
                        (rho*u[2]*u[2]+P)*BJ[2];
    vg[i+(3+5*1)*Q]  =  rho*u[2]*u[0]*BJ[3] + rho*u[2]*u[1]*BJ[4] +
                        (rho*u[2]*u[2]+P)*BJ[5];
    vg[i+(3+5*2)*Q]  =  rho*u[2]*u[0]*BJ[6] + rho*u[2]*u[1]*BJ[7] +
                        (rho*u[2]*u[2]+P)*BJ[8];
    // ---- Fuvisc
    vg[i+(1+5*0)*Q] -= Fu[0]*BBJ[0] + Fu[1]*BBJ[1] + Fu[2]*BBJ[2];
    vg[i+(1+5*1)*Q] -= Fu[0]*BBJ[1] + Fu[1]*BBJ[3] + Fu[2]*BBJ[4];
    vg[i+(1+5*2)*Q] -= Fu[0]*BBJ[2] + Fu[1]*BBJ[4] + Fu[2]*BBJ[5];
    vg[i+(2+5*0)*Q] -= Fu[1]*BBJ[0] + Fu[3]*BBJ[1] + Fu[4]*BBJ[2];
    vg[i+(2+5*1)*Q] -= Fu[1]*BBJ[1] + Fu[3]*BBJ[3] + Fu[4]*BBJ[4];
    vg[i+(2+5*2)*Q] -= Fu[1]*BBJ[2] + Fu[3]*BBJ[4] + Fu[4]*BBJ[5];
    vg[i+(3+5*0)*Q] -= Fu[2]*BBJ[0] + Fu[4]*BBJ[1] + Fu[5]*BBJ[2];
    vg[i+(3+5*1)*Q] -= Fu[2]*BBJ[1] + Fu[4]*BBJ[3] + Fu[5]*BBJ[4];
    vg[i+(3+5*2)*Q] -= Fu[2]*BBJ[2] + Fu[4]*BBJ[4] + Fu[5]*BBJ[5];
    // ---- -rho g k
    v[i+3*Q] = - rho*g*J;

    // -- Total Energy
    // ---- (E + P) u
    vg[i+(4+5*0)*Q]  = (E + P)*(u[0]*BJ[0] + u[1]*BJ[1] + u[2]*BJ[2]);
    vg[i+(4+5*1)*Q]  = (E + P)*(u[0]*BJ[3] + u[1]*BJ[4] + u[2]*BJ[5]);
    vg[i+(4+5*2)*Q]  = (E + P)*(u[0]*BJ[6] + u[1]*BJ[7] + u[2]*BJ[8]);
    // ---- Fevisc
    vg[i+(4+5*0)*Q] -= Fe[0]*BBJ[0] + Fe[1]*BBJ[1] + Fe[2]*BBJ[2];
    vg[i+(4+5*1)*Q] -= Fe[0]*BBJ[1] + Fe[1]*BBJ[3] + Fe[2]*BBJ[4];
    vg[i+(4+5*2)*Q] -= Fe[0]*BBJ[2] + Fe[1]*BBJ[4] + Fe[2]*BBJ[5];

  } // End Quadrature Point Loop

  // Return
  return 0;
}
