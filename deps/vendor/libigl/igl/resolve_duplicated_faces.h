// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
#ifndef IGL_COPYLEFT_RESOLVE_DUPLICATED_FACES
#define IGL_COPYLEFT_RESOLVE_DUPLICATED_FACES

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl {

















  template<
    typename DerivedF1,
    typename DerivedF2,
    typename DerivedJ >
  IGL_INLINE void resolve_duplicated_faces(
      const Eigen::MatrixBase<DerivedF1>& F1,
      Eigen::PlainObjectBase<DerivedF2>& F2,
      Eigen::PlainObjectBase<DerivedJ>& J);

}

#ifndef IGL_STATIC_LIBRARY
#  include "resolve_duplicated_faces.cpp"
#endif

#endif
