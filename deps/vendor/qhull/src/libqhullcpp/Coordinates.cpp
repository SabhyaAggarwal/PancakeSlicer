/****************************************************************************
**
** Copyright (c) 2009-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/Coordinates.cpp#4 $$Change: 2066 $
** $DateTime: 2016/01/18 19:29:17 $$Author: bbarber $
**
****************************************************************************/

#include "libqhullcpp/Coordinates.h"

#include "libqhullcpp/functionObjects.h"
#include "libqhullcpp/QhullError.h"

#include <iostream>
#include <iterator>
#include <algorithm>

#ifdef _MSC_VER
#endif

namespace orgQhull {

#

#

#


Coordinates Coordinates::
mid(countT idx, countT length) const
{
    countT newLength= length;
    if(length<0 || idx+length > count()){
        newLength= count()-idx;
    }
    Coordinates result;
    if(newLength>0){
        std::copy(begin()+idx, begin()+(idx+newLength), std::back_inserter(result));
    }
    return result;
}

coordT Coordinates::
value(countT idx, const coordT &defaultValue) const
{
    return ((idx < 0 || idx >= count()) ? defaultValue : (*this)[idx]);
}

#

Coordinates Coordinates::
operator+(const Coordinates &other) const
{
    Coordinates result(*this);
    std::copy(other.begin(), other.end(), std::back_inserter(result));
    return result;
}

Coordinates & Coordinates::
operator+=(const Coordinates &other)
{
    if(&other==this){
        Coordinates clone(other);
        std::copy(clone.begin(), clone.end(), std::back_inserter(*this));
    }else{
        std::copy(other.begin(), other.end(), std::back_inserter(*this));
    }
    return *this;
}

#

void Coordinates::
append(int pointDimension, coordT *c)
{
    if(c){
        coordT *p= c;
        for(int i= 0; i<pointDimension; ++i){
            coordinate_array.push_back(*p++);
        }
    }
}

coordT Coordinates::
takeAt(countT idx)
{
    coordT c= at(idx);
    erase(begin()+idx);
    return c;
}

coordT Coordinates::
takeLast()
{
    coordT c= last();
    removeLast();
    return c;
}

void Coordinates::
swap(countT idx, countT other)
{
    coordT c= at(idx);
    at(idx)= at(other);
    at(other)= c;
}

#

bool Coordinates::
contains(const coordT &t) const
{
    CoordinatesIterator i(*this);
    return i.findNext(t);
}

countT Coordinates::
count(const coordT &t) const
{
    CoordinatesIterator i(*this);
    countT result= 0;
    while(i.findNext(t)){
        ++result;
    }
    return result;
}

countT Coordinates::
indexOf(const coordT &t, countT from) const
{
    if(from<0){
        from += count();
        if(from<0){
            from= 0;
        }
    }
    if(from<count()){
        const_iterator i= begin()+from;
        while(i!=constEnd()){
            if(*i==t){
                return (static_cast<countT>(i-begin()));
            }
            ++i;
        }
    }
    return -1;
}

countT Coordinates::
lastIndexOf(const coordT &t, countT from) const
{
    if(from<0){
        from += count();
    }else if(from>=count()){
        from= count()-1;
    }
    if(from>=0){
        const_iterator i= begin()+from+1;
        while(i-- != constBegin()){
            if(*i==t){
                return (static_cast<countT>(i-begin()));
            }
        }
    }
    return -1;
}

void Coordinates::
removeAll(const coordT &t)
{
    MutableCoordinatesIterator i(*this);
    while(i.findNext(t)){
        i.remove();
    }
}

}

#

using std::endl;
using std::istream;
using std::ostream;
using std::string;
using std::ws;
using orgQhull::Coordinates;

ostream &
operator<<(ostream &os, const Coordinates &cs)
{
    Coordinates::const_iterator c= cs.begin();
    for(countT i=cs.count(); i--; ){
        os << *c++ << " ";
    }
    return os;
}
