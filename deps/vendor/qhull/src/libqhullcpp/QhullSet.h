/****************************************************************************
**
** Copyright (c) 2008-2015 C.B. Barber. All rights reserved.
** $Id: //main/2015/qhull/src/libqhullcpp/QhullSet.h#6 $$Change: 2079 $
** $DateTime: 2016/02/07 17:43:34 $$Author: bbarber $
**
****************************************************************************/

#ifndef QhullSet_H
#define QhullSet_H

#include "libqhull_r/qhull_ra.h"
#include "libqhullcpp/QhullError.h"
#include "libqhullcpp/QhullQh.h"

#include <cstddef>

#ifndef QHULL_NO_STL
#include <vector>
#endif

#ifdef QHULL_USES_QT
 #include <QtCore/QList>
#endif

namespace orgQhull {

#
    class Qhull;

#
    class QhullSetBase;






class QhullSetBase {

private:
#
    setT *              qh_set;
    QhullQh *           qh_qh;

#
    static setT         s_empty_set;

public:
#
                        QhullSetBase(const Qhull &q, setT *s);
                        QhullSetBase(QhullQh *qqh, setT *s) : qh_set(s ? s : &s_empty_set), qh_qh(qqh) {}

                        QhullSetBase(const QhullSetBase &other) : qh_set(other.qh_set), qh_qh(other.qh_qh) {}
    QhullSetBase &      operator=(const QhullSetBase &other) { qh_set= other.qh_set; qh_qh= other.qh_qh; return *this; }
                        ~QhullSetBase() {}

private:

                        QhullSetBase() {}
public:

#
    countT              count() const { return QhullSetBase::count(qh_set); }
    void                defineAs(setT *s) { qh_set= s ? s : &s_empty_set; }
    void                forceEmpty() { qh_set= &s_empty_set; }
    setT *              getSetT() const { return qh_set; }
    bool                isEmpty() const { return SETempty_(qh_set); }
    QhullQh *           qh() const { return qh_qh; }
    setT **             referenceSetT() { return &qh_set; }
    size_t              size() const { return QhullSetBase::count(qh_set); }

#
protected:
    void **             beginPointer() const { return &qh_set->e[0].p; }
    void **             elementPointer(countT idx) const { QHULL_ASSERT(idx>=0 && idx<qh_set->maxsize); return &SETelem_(qh_set, idx); }

    void **             endPointer() const { return qh_setendpointer(qh_set); }

#
public:
    static countT       count(const setT *set);

    static bool         isEmpty(const setT *s) { return SETempty_(s); }
};










template <typename T>
class QhullSet : public QhullSetBase {

private:
#

#
    static setT         s_empty_set;

public:
#
    class iterator;
    class const_iterator;
    typedef typename QhullSet<T>::iterator Iterator;
    typedef typename QhullSet<T>::const_iterator ConstIterator;

#
                        QhullSet<T>(const Qhull &q, setT *s) : QhullSetBase(q, s) { }
                        QhullSet<T>(QhullQh *qqh, setT *s) : QhullSetBase(qqh, s) { }


                        QhullSet<T>(const QhullSet<T> &other) : QhullSetBase(other) {}
    QhullSet<T> &       operator=(const QhullSet<T> &other) { QhullSetBase::operator=(other); return *this; }
                        ~QhullSet<T>() {}

private:

                        QhullSet<T>();
public:

#

#ifndef QHULL_NO_STL
    std::vector<T> toStdVector() const;
#endif
#ifdef QHULL_USES_QT
    QList<typename T> toQList() const;
#endif

#
    using QhullSetBase::count;
    using QhullSetBase::isEmpty;

    bool                operator==(const QhullSet<T> &other) const { return qh_setequal(getSetT(), other.getSetT()); }
    bool                operator!=(const QhullSet<T> &other) const { return !operator==(other); }

#

    const T             at(countT idx) const { return operator[](idx); }

    const T             back() const { return last(); }
    T                   back() { return last(); }

    const typename T::base_type * constData() const { return reinterpret_cast<const typename T::base_type *>(beginPointer()); }
    typename T::base_type *     data() { return reinterpret_cast<typename T::base_type *>(beginPointer()); }
    const typename T::base_type *data() const { return reinterpret_cast<const typename T::base_type *>(beginPointer()); }
    typename T::base_type *     endData() { return reinterpret_cast<typename T::base_type *>(endPointer()); }
    const typename T::base_type * endData() const { return reinterpret_cast<const typename T::base_type *>(endPointer()); }

    const T             first() const { QHULL_ASSERT(!isEmpty()); return T(qh(), *data()); }
    T                   first() { QHULL_ASSERT(!isEmpty()); return T(qh(), *data()); }

    const T             front() const { return first(); }
    T                   front() { return first(); }

    const T             last() const { QHULL_ASSERT(!isEmpty()); return T(qh(), *(endData()-1)); }
    T                   last() { QHULL_ASSERT(!isEmpty()); return T(qh(), *(endData()-1)); }


    const T             operator[](countT idx) const { const typename T::base_type *p= reinterpret_cast<typename T::base_type *>(elementPointer(idx)); QHULL_ASSERT(idx>=0 && p < endData()); return T(qh(), *p); }
    T                   operator[](countT idx) { typename T::base_type *p= reinterpret_cast<typename T::base_type *>(elementPointer(idx)); QHULL_ASSERT(idx>=0 && p < endData()); return T(qh(), *p); }
    const T             second() const { return operator[](1); }
    T                   second() { return operator[](1); }
    T                   value(countT idx) const;
    T                   value(countT idx, const T &defaultValue) const;

#

#
    iterator            begin() { return iterator(qh(), reinterpret_cast<typename T::base_type *>(beginPointer())); }
    const_iterator      begin() const { return const_iterator(qh(), data()); }
    const_iterator      constBegin() const { return const_iterator(qh(), data()); }
    const_iterator      constEnd() const { return const_iterator(qh(), endData()); }
    iterator            end() { return iterator(qh(), endData()); }
    const_iterator      end() const { return const_iterator(qh(), endData()); }

#
    bool                contains(const T &t) const;
    countT              count(const T &t) const;
    countT              indexOf(const T &t) const {   return qh_setindex(getSetT(), t.getBaseT()); }
    countT              lastIndexOf(const T &t) const;


    class iterator {
        friend class const_iterator;
    private:
        typename T::base_type *  i;
        QhullQh *       qh_qh;

    public:
        typedef ptrdiff_t       difference_type;
        typedef std::bidirectional_iterator_tag  iterator_category;
        typedef T               value_type;

                        iterator(QhullQh *qqh, typename T::base_type *p) : i(p), qh_qh(qqh) {}
                        iterator(const iterator &o) : i(o.i), qh_qh(o.qh_qh) {}
        iterator &      operator=(const iterator &o) { i= o.i; qh_qh= o.qh_qh; return *this; }


        T               operator*() const { return T(qh_qh, *i); }


        T               operator[](countT idx) const { return T(qh_qh, *(i+idx)); }
        bool            operator==(const iterator &o) const { return i == o.i; }
        bool            operator!=(const iterator &o) const { return !operator==(o); }
        bool            operator==(const const_iterator &o) const { return (i==reinterpret_cast<const iterator &>(o).i); }
        bool            operator!=(const const_iterator &o) const { return !operator==(o); }


        countT          operator-(const iterator &o) const { return (countT)(i-o.i); }
        bool            operator>(const iterator &o) const { return i>o.i; }
        bool            operator<=(const iterator &o) const { return !operator>(o); }
        bool            operator<(const iterator &o) const { return i<o.i; }
        bool            operator>=(const iterator &o) const { return !operator<(o); }
        bool            operator>(const const_iterator &o) const { return (i > reinterpret_cast<const iterator &>(o).i); }
        bool            operator<=(const const_iterator &o) const { return !operator>(o); }
        bool            operator<(const const_iterator &o) const { return (i < reinterpret_cast<const iterator &>(o).i); }
        bool            operator>=(const const_iterator &o) const { return !operator<(o); }


        iterator &      operator++() { ++i; return *this; }
        iterator        operator++(int) { iterator o= *this; ++i; return o; }
        iterator &      operator--() { --i; return *this; }
        iterator        operator--(int) { iterator o= *this; --i; return o; }
        iterator        operator+(countT j) const { return iterator(qh_qh, i+j); }
        iterator        operator-(countT j) const { return operator+(-j); }
        iterator &      operator+=(countT j) { i += j; return *this; }
        iterator &      operator-=(countT j) { i -= j; return *this; }
    };

    class const_iterator {
    private:
        const typename T::base_type *  i;
        QhullQh *       qh_qh;

    public:
        typedef ptrdiff_t       difference_type;
        typedef std::random_access_iterator_tag  iterator_category;
        typedef T               value_type;

                        const_iterator(QhullQh *qqh, const typename T::base_type * p) : i(p), qh_qh(qqh) {}
                        const_iterator(const const_iterator &o) : i(o.i), qh_qh(o.qh_qh) {}
                        const_iterator(const iterator &o) : i(o.i), qh_qh(o.qh_qh) {}
        const_iterator &operator=(const const_iterator &o) { i= o.i; qh_qh= o.qh_qh; return *this; }


        const T         operator*() const { return T(qh_qh, *i); }
        const T         operator[](countT idx) const { return T(qh_qh, *(i+idx)); }

        bool            operator==(const const_iterator &o) const { return i == o.i; }
        bool            operator!=(const const_iterator &o) const { return !operator==(o); }


        countT          operator-(const const_iterator &o) { return (countT)(i-o.i); }
        bool            operator>(const const_iterator &o) const { return i>o.i; }
        bool            operator<=(const const_iterator &o) const { return !operator>(o); }
        bool            operator<(const const_iterator &o) const { return i<o.i; }
        bool            operator>=(const const_iterator &o) const { return !operator<(o); }


        const_iterator &operator++() { ++i; return *this; }
        const_iterator  operator++(int) { const_iterator o= *this; ++i; return o; }
        const_iterator &operator--() { --i; return *this; }
        const_iterator  operator--(int) { const_iterator o= *this; --i; return o; }
        const_iterator  operator+(int j) const { return const_iterator(qh_qh, i+j); }
        const_iterator  operator-(int j) const { return operator+(-j); }
        const_iterator &operator+=(int j) { i += j; return *this; }
        const_iterator &operator-=(int j) { i -= j; return *this; }
    };

};



template <typename T>
class QhullSetIterator {

#
    typedef typename QhullSet<T>::const_iterator const_iterator;

private:
#
    const typename T::base_type *  i;
    const typename T::base_type *  begin_i;
    const typename T::base_type *  end_i;
    QhullQh *                qh_qh;

public:
#
                        QhullSetIterator<T>(const QhullSet<T> &s) : i(s.data()), begin_i(i), end_i(s.endData()), qh_qh(s.qh()) {}
                        QhullSetIterator<T>(const QhullSetIterator<T> &o) : i(o.i), begin_i(o.begin_i), end_i(o.end_i), qh_qh(o.qh_qh) {}
    QhullSetIterator<T> &operator=(const QhullSetIterator<T> &o) { i= o.i; begin_i= o.begin_i; end_i= o.end_i; qh_qh= o.qh_qh; return *this; }

#
    countT              countRemaining() { return (countT)(end_i-i); }

#
    bool                findNext(const T &t);
    bool                findPrevious(const T &t);

#
    bool                hasNext() const { return i != end_i; }
    bool                hasPrevious() const { return i != begin_i; }
    T                   next() { return T(qh_qh, *i++); }
    T                   peekNext() const { return T(qh_qh, *i); }
    T                   peekPrevious() const { const typename T::base_type *p = i; return T(qh_qh, *--p); }
    T                   previous() { return T(qh_qh, *--i); }
    void                toBack() { i = end_i; }
    void                toFront() { i = begin_i; }
};

#

#



#ifndef QHULL_NO_STL
template <typename T>
std::vector<T> QhullSet<T>::
toStdVector() const
{
	typename QhullSet<T>::const_iterator i = begin();
	typename QhullSet<T>::const_iterator e = end();
    std::vector<T> vs;
    while(i!=e){
        vs.push_back(*i++);
    }
    return vs;
}
#endif

#ifdef QHULL_USES_QT
template <typename T>
QList<T> QhullSet<T>::
toQList() const
{
    QhullSet<T>::const_iterator i= begin();
    QhullSet<T>::const_iterator e= end();
    QList<T> vs;
    while(i!=e){
        vs.append(*i++);
    }
    return vs;
}
#endif

#

template <typename T>
T QhullSet<T>::
value(countT idx) const
{

    const typename T::base_type *p= reinterpret_cast<const typename T::base_type *>(&SETelem_(getSetT(), idx));
    return (idx>=0 && p<endData()) ? T(qh(), *p) : T(qh());
}

template <typename T>
T QhullSet<T>::
value(countT idx, const T &defaultValue) const
{

    const typename T::base_type *p= reinterpret_cast<const typename T::base_type *>(&SETelem_(getSetT(), idx));
    return (idx>=0 && p<endData() ? T(qh(), *p) : defaultValue);
}

#

template <typename T>
bool QhullSet<T>::
contains(const T &t) const
{
    setT *s= getSetT();
    void *p= t.getBaseT();
    int result= qh_setin(s, p);
    return result!=0;
}

template <typename T>
countT QhullSet<T>::
count(const T &t) const
{
    countT n= 0;
    const typename T::base_type *i= data();
    const typename T::base_type *e= endData();
    typename T::base_type p= t.getBaseT();
    while(i<e){
        if(*i==p){
            n++;
        }
        i++;
    }
    return n;
}

template <typename T>
countT QhullSet<T>::
lastIndexOf(const T &t) const
{
    const typename T::base_type *b= data();
    const typename T::base_type *i= endData();
    typename T::base_type p= t.getBaseT();
    while(--i>=b){
        if(*i==p){
            break;
        }
    }
    return (countT)(i-b);
}

#

template <typename T>
bool QhullSetIterator<T>::
findNext(const T &t)
{
    typename T::base_type p= t.getBaseT();
    while(i!=end_i){
        if(*(++i)==p){
            return true;
        }
    }
    return false;
}

template <typename T>
bool QhullSetIterator<T>::
findPrevious(const T &t)
{
    typename T::base_type p= t.getBaseT();
    while(i!=begin_i){
        if(*(--i)==p){
            return true;
        }
    }
    return false;
}

}


#

template <typename T>
std::ostream &
operator<<(std::ostream &os, const orgQhull::QhullSet<T> &qs)
{
    const typename T::base_type *i= qs.data();
    const typename T::base_type *e= qs.endData();
    while(i!=e){
        os << T(qs.qh(), *i++);
    }
    return os;
}

#endif
