#ifndef TRY_CATCH_SIGNAL_HPP
#define TRY_CATCH_SIGNAL_HPP

#ifdef _MSC_VER
#include "TryCatchSignalSEH.hpp"
#else

#include <csignal>

using SignalT = decltype (SIGSEGV);

template<class TryFn, class CatchFn, int N>
void try_catch_signal(const SignalT (& )[N], TryFn &&fn, CatchFn && )
{
    fn();
}
#endif

#endif
