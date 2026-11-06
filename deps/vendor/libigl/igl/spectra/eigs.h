// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2023 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SPECTRA_EIGS_H
#define IGL_SPECTRA_EIGS_H
#include "../igl_inline.h"
#include "../eigs.h"
#include <Eigen/Core>
#include <Eigen/Sparse>
#include <Eigen/SparseLU>

namespace igl
{
  namespace spectra
  {



















    template <
      typename EigsScalar,
      typename DerivedU,
      typename DerivedS,
      typename Solver = Eigen::SparseLU<Eigen::SparseMatrix<EigsScalar>> >
    IGL_INLINE bool eigs(
      const Eigen::SparseMatrix<EigsScalar> & A,
      const Eigen::SparseMatrix<EigsScalar> & B,
      const int k,
      const igl::EigsType type,
      Eigen::PlainObjectBase<DerivedU> & U,
      Eigen::PlainObjectBase<DerivedS> & S);


    template <
      typename EigsScalar,
      typename DerivedU,
      typename DerivedS,
      typename Solver = Eigen::SparseLU<Eigen::SparseMatrix<EigsScalar>> >
    IGL_INLINE bool eigs(
      const Eigen::SparseMatrix<EigsScalar> & A,
      const Eigen::SparseMatrix<EigsScalar> & B,
      const int k,
      const EigsScalar sigma,
      Eigen::PlainObjectBase<DerivedU> & U,
      Eigen::PlainObjectBase<DerivedS> & S);
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "eigs.cpp"
#endif
#endif
