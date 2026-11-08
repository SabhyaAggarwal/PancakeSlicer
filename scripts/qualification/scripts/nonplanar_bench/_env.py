"""Make numpy importable under `python3 -I`.

Isolated mode drops the user site directory, which is where `pip install --user` puts numpy. If
numpy is not importable otherwise, append (never prepend) the user site so the standard library
keeps precedence and the current directory stays off sys.path.
"""
import sys
import numpy

_TRIM = None
_TRIM_CHECKED = False


def release_memory():
    """Return freed array buffers before another memory-heavy measurement phase."""
    import gc
    gc.collect()
    global _TRIM, _TRIM_CHECKED
    if not _TRIM_CHECKED:
        _TRIM_CHECKED = True
        if sys.platform.startswith("linux"):
            import ctypes
            _TRIM = getattr(ctypes.CDLL(None), "malloc_trim", None)
            if _TRIM is not None:
                _TRIM.argtypes = [ctypes.c_size_t]
                _TRIM.restype = ctypes.c_int
    if _TRIM is not None:
        _TRIM(0)
