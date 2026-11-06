// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_REDRUM_H
#define IGL_REDRUM_H








#ifdef IGL_REDRUM_NOOP


#define NORUM(X)     X
#define REDRUM(X)     X
#define GREENRUM(X)   X
#define YELLOWRUM(X)  X
#define BLUERUM(X)    X
#define MAGENTARUM(X) X
#define CYANRUM(X)    X

#define REDGIN(X)     X
#define GREENGIN(X)   X
#define YELLOWGIN(X)  X
#define BLUEGIN(X)    X
#define MAGENTAGIN(X) X
#define CYANGIN(X)    X

#else









#define REDRUM(X)      "\e[1m\e[31m"<<X<<"\e[m"

#define NORUM(X)       ""<<X<<""
#define GREENRUM(X)    "\e[1m\e[32m"<<X<<"\e[m"
#define YELLOWRUM(X)   "\e[1m\e[33m"<<X<<"\e[m"
#define BLUERUM(X)     "\e[1m\e[34m"<<X<<"\e[m"
#define MAGENTARUM(X)  "\e[1m\e[35m"<<X<<"\e[m"
#define CYANRUM(X)     "\e[1m\e[36m"<<X<<"\e[m"

#define REDGIN(X)      "\e[31m"<<X<<"\e[m"
#define GREENGIN(X)    "\e[32m"<<X<<"\e[m"
#define YELLOWGIN(X)   "\e[33m"<<X<<"\e[m"
#define BLUEGIN(X)     "\e[34m"<<X<<"\e[m"
#define MAGENTAGIN(X)  "\e[35m"<<X<<"\e[m"
#define CYANGIN(X)     "\e[36m"<<X<<"\e[m"
#endif

#endif
