/// @file
/// Test polynomial interpolation in 1D
/// \test Test polynomial interpolation in 1D
#include <ceed.h>
#include <math.h>

#define ALEN(a) (sizeof(a) / sizeof((a)[0]))

static CeedScalar PolyEval(CeedScalar x, CeedInt n, const CeedScalar *p) {
  CeedScalar y = p[n-1];
  for (CeedInt i=n-2; i>=0; i--) y = y*x + p[i];
  return y;
}

int main(int argc, char **argv) {
  Ceed ceed;
  CeedVector X, Xq, U, Uq;
  CeedBasis bxl, bul, bxg, bug;
  CeedInt Q = 6;
  const CeedScalar p[] = {1, 2, 3, 4, 5, 6}; // 1 + 2x + 3x^2 + ...
  const CeedScalar x[] = {-1, 1};
  const CeedScalar *xq, *u;
  CeedScalar uq[Q];

  CeedInit(argv[1], &ceed);

  CeedVectorCreate(ceed, 2, &X);
  CeedVectorSetArray(X, CEED_MEM_HOST, CEED_USE_POINTER, (CeedScalar *)&x);
  CeedVectorCreate(ceed, Q, &Xq);
  CeedVectorSetValue(Xq, 0);
  CeedVectorCreate(ceed, Q, &U);
  CeedVectorSetValue(U, 0);
  CeedVectorCreate(ceed, Q, &Uq);
  CeedVectorSetValue(Uq, 0);

  CeedBasisCreateTensorH1Lagrange(ceed, 1,  1, 2, Q, CEED_GAUSS_LOBATTO, &bxl);
  CeedBasisCreateTensorH1Lagrange(ceed, 1, 1, Q, Q, CEED_GAUSS_LOBATTO, &bul);

  CeedBasisApply(bxl, 1, CEED_NOTRANSPOSE, CEED_EVAL_INTERP, X, Xq);

  CeedVectorGetArrayRead(Xq, CEED_MEM_HOST, &xq);
  for (CeedInt i=0; i<Q; i++) uq[i] = PolyEval(xq[i], ALEN(p), p);
  CeedVectorRestoreArrayRead(Xq, &xq);
  CeedVectorSetArray(Uq, CEED_MEM_HOST, CEED_USE_POINTER, (CeedScalar *)&uq);

  // This operation is the identity because the quadrature is collocated
  CeedBasisApply(bul, 1, CEED_TRANSPOSE, CEED_EVAL_INTERP, Uq, U);

  CeedBasisCreateTensorH1Lagrange(ceed, 1, 1, 2, Q, CEED_GAUSS, &bxg);
  CeedBasisCreateTensorH1Lagrange(ceed, 1, 1, Q, Q, CEED_GAUSS, &bug);

  CeedBasisApply(bxg, 1, CEED_NOTRANSPOSE, CEED_EVAL_INTERP, X, Xq);
  CeedBasisApply(bug, 1, CEED_NOTRANSPOSE, CEED_EVAL_INTERP, U, Uq);

  CeedVectorGetArrayRead(Xq, CEED_MEM_HOST, &xq);
  CeedVectorGetArrayRead(Uq, CEED_MEM_HOST, &u);
  for (CeedInt i=0; i<Q; i++) {
    CeedScalar px = PolyEval(xq[i], ALEN(p), p);
    if ((fabs(u[i] - px) > 1e-14)) {
      printf("%f != %f=p(%f)\n", uq[i], px, xq[i]);
    }
  }
  CeedVectorRestoreArrayRead(Xq, &xq);
  CeedVectorRestoreArrayRead(Uq, &u);

  CeedVectorDestroy(&X);
  CeedVectorDestroy(&Xq);
  CeedVectorDestroy(&U);
  CeedVectorDestroy(&Uq);
  CeedBasisDestroy(&bxl);
  CeedBasisDestroy(&bul);
  CeedBasisDestroy(&bxg);
  CeedBasisDestroy(&bug);
  CeedDestroy(&ceed);
  return 0;
}
