// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "writeTGF.h"
#include <cstdio>

IGL_INLINE bool igl::writeTGF(
  const std::string tgf_filename,
  const std::vector<std::vector<double> > & C,
  const std::vector<std::vector<int> > & E)
{
  FILE * tgf_file = fopen(tgf_filename.c_str(),"w");
  if(NULL==tgf_file)
  {
    printf("IOError: %s could not be opened\n",tgf_filename.c_str());
    return false;
  }

  for(int i = 0; i<(int)C.size();i++)
  {
    assert(C[i].size() == 3);



    fprintf(tgf_file,
      "%4d "
      "%10.17g %10.17g %10.17g "

      "\n",
      i+1,
      C[i][0], C[i][1], C[i][2]);
  }


  fprintf(tgf_file,"#\n");


  for(int i = 0;i<(int)E.size();i++)
  {
    assert(E[i].size()==2);
    fprintf(tgf_file,"%4d %4d\n",
      E[i][0]+1,
      E[i][1]+1);
  }


  fprintf(tgf_file,"#\n");

  fclose(tgf_file);

  return true;
}

#ifndef IGL_NO_EIGEN
#include "matrix_to_list.h"

IGL_INLINE bool igl::writeTGF(
  const std::string tgf_filename,
  const Eigen::MatrixXd & C,
  const Eigen::MatrixXi & E)
{
  using namespace std;
  vector<vector<double> > vC;
  vector<vector<int> > vE;
  matrix_to_list(C,vC);
  matrix_to_list(E,vE);
  return writeTGF(tgf_filename,vC,vE);
}
#endif
