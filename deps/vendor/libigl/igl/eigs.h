// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EIGS_H
#define IGL_EIGS_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Sparse>

namespace igl
{

  enum EigsType
  {

    EIGS_TYPE_SM = 0,

    EIGS_TYPE_LM = 1,

    NUM_EIGS_TYPES = 2
  };





















  template <
    typename Atype,
    typename Btype,
    typename DerivedU,
    typename DerivedS>
  IGL_INLINE bool eigs(
    const Eigen::SparseMatrix<Atype> & A,
    const Eigen::SparseMatrix<Btype> & B,
    const size_t k,
    const EigsType type,
    Eigen::PlainObjectBase<DerivedU> & sU,
    Eigen::PlainObjectBase<DerivedS> & sS);
}

#ifndef IGL_STATIC_LIBRARY
#include "eigs.cpp"
#endif
#endif
