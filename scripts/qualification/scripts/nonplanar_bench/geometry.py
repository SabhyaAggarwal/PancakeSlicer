"""Analytic benchmark geometry: surfaces, domains and watertight mesh builders.

The same surface specs are written to parts.json by make_parts.py and evaluated by metrics.py,
so the error metric measures against exactly the geometry that was meshed. numpy only.
"""
import math
import struct

import numpy as np





def naca4_profile(code="2412", chord=100.0, n=160):
    """Closed-trailing-edge NACA 4-digit section. Returns (upper, lower) as (n, 2) arrays of
    (x, z), both running from the leading edge to the trailing edge."""
    m, p, t = int(code[0]) / 100, int(code[1]) / 10, int(code[2:]) / 100
    beta = np.linspace(0, math.pi, n)
    x = (1 - np.cos(beta)) / 2
    yt = 5 * t * (0.2969 * np.sqrt(x) - 0.1260 * x - 0.3516 * x**2 + 0.2843 * x**3 - 0.1036 * x**4)
    fore = x < p
    yc = np.where(fore, m / p**2 * (2 * p * x - x**2), m / (1 - p)**2 * (1 - 2 * p + 2 * p * x - x**2))
    dyc = np.where(fore, 2 * m / p**2 * (p - x), 2 * m / (1 - p)**2 * (p - x))
    th = np.arctan(dyc)
    upper = np.stack([x - yt * np.sin(th), yc + yt * np.cos(th)], 1) * chord
    lower = np.stack([x + yt * np.sin(th), yc - yt * np.cos(th)], 1) * chord
    return upper, lower


def slope_star_rise(r, spec):
    """Height above the base of a slope-star wedge at radius r: piecewise linear, slope stepping
    through spec["slopes_deg"] in equal radial steps from r0 to r1."""
    r0, r1, slopes = spec["r0"], spec["r1"], np.radians(spec["slopes_deg"])
    step = (r1 - r0) / len(slopes)
    rr = np.clip(np.asarray(r, float) - r0, 0, r1 - r0)
    out = np.zeros_like(rr)
    for k, s in enumerate(slopes):
        out += np.clip(rr - k * step, 0, step) * math.tan(s)
    return out


def surface_z(spec, x, y):
    """Height of an analytic surface. NaN where the surface is undefined."""
    x, y = np.asarray(x, float), np.asarray(y, float)
    kind = spec["kind"]
    if kind == "plane":
        a = math.radians(spec.get("azimuth_deg", 0.0))
        return spec["z0"] + math.tan(math.radians(spec["slope_deg"])) * (x * math.cos(a) + y * math.sin(a))
    if kind == "sphere_cap":
        r2 = (x - spec["cx"])**2 + (y - spec["cy"])**2
        return spec["zc"] + np.sqrt(np.where(r2 <= spec["R"]**2, spec["R"]**2 - r2, np.nan))
    if kind == "saddle":
        return spec["z0"] + (x**2 - y**2) / spec["k"]
    if kind == "sine_x":
        return spec["z0"] + spec["A"] * np.sin(2 * math.pi * x / spec["wavelength"])
    if kind == "profile_x":

        px, pz = np.asarray(spec["x"]), np.asarray(spec["z"])
        return np.where((x >= px[0]) & (x <= px[-1]), np.interp(x, px, pz), np.nan) + 0 * y
    if kind == "cylinder_y_under":

        d = spec["R"]**2 - (x - spec["cx"])**2
        return spec["zc"] + np.sqrt(np.where(d >= 0, d, np.nan)) + 0 * y
    if kind == "flat":
        return np.full(np.broadcast(x, y).shape, float(spec["z0"]))
    if kind == "grid":
        return grid_sample(spec, "z", x, y)
    if kind == "slope_star":
        r = np.hypot(x, y)
        phi = np.degrees(np.arctan2(y, x))
        rel = (phi - spec["phase_deg"] + 180) % spec["pitch_deg"]
        rel = np.minimum(rel, spec["pitch_deg"] - rel)
        on = (rel <= spec["half_width_deg"] + 1e-6) & (r >= spec["r0"]) & (r <= spec["r1"])
        return np.where(on, spec["base"] + slope_star_rise(r, spec), spec["base"])
    raise ValueError("unknown surface kind %r" % kind)


def surface_grad(spec, x, y, h=1e-3):
    """Central-difference gradient (dz/dx, dz/dy)."""
    gx = (surface_z(spec, x + h, y) - surface_z(spec, x - h, y)) / (2 * h)
    gy = (surface_z(spec, x, y + h) - surface_z(spec, x, y - h)) / (2 * h)
    return gx, gy


def domain_mask(dom, x, y, inset=0.0):
    """True inside the XY domain shrunk by `inset` mm."""
    x, y = np.asarray(x, float), np.asarray(y, float)
    kind = dom["kind"]
    if kind == "circle":
        return np.hypot(x - dom["cx"], y - dom["cy"]) <= dom["r"] - inset
    if kind == "rect":
        return ((x >= dom["x0"] + inset) & (x <= dom["x1"] - inset)
                & (y >= dom["y0"] + inset) & (y <= dom["y1"] - inset))
    if kind == "union":
        out = np.zeros(np.broadcast(x, y).shape, bool)
        for d in dom["parts"]:
            out |= domain_mask(d, x, y, inset)
        return out
    if kind == "difference":
        return domain_mask(dom["a"], x, y, inset) & ~domain_mask(dom["b"], x, y, -inset)
    if kind == "mask":

        d = grid_sample(dom, "dist", x, y, nearest=True)
        return (d >= inset) & (grid_sample(dom, "ok", x, y, nearest=True) > 0.5)
    raise ValueError("unknown domain kind %r" % kind)






_GRIDS = {}


def _grid(spec):
    path = spec["npz"]
    if path not in _GRIDS:
        with np.load(path) as d:
            _GRIDS[path] = {k: d[k] for k in d.files}
    return _GRIDS[path]


def grid_bbox(spec):
    d = _grid(spec)
    lo = np.asarray(d["origin"], float)
    return lo, lo + (np.array(d["z"].shape) - 1) * float(d["pitch"])


def grid_discontinuous(spec, x, y):
    """Queries whose four interpolation nodes cross separate reference faces."""
    d = _grid(spec)
    if "discontinuous" not in d:

        return np.zeros(np.broadcast(x, y).shape, bool)
    u, v = (np.asarray(x) - d["origin"][0]) / d["pitch"], (np.asarray(y) - d["origin"][1]) / d["pitch"]
    i, j = np.floor(u).astype(int), np.floor(v).astype(int)
    nx, ny = d["discontinuous"].shape
    ok = (i >= 0) & (j >= 0) & (i < nx) & (j < ny)
    return ok & d["discontinuous"][np.clip(i, 0, nx - 1), np.clip(j, 0, ny - 1)]


def grid_sample(spec, field, x, y, nearest=False):
    """Bilinear (or nearest-node) sample of an .npz field; NaN (or 0) outside or next to NoData."""
    d = _grid(spec)
    a, o, p = d[field], np.asarray(d["origin"], float), float(d["pitch"])
    u, v = (np.asarray(x, float) - o[0]) / p, (np.asarray(y, float) - o[1]) / p
    nx, ny = a.shape
    if nearest:
        i, j = np.rint(u).astype(int), np.rint(v).astype(int)
        ok = (i >= 0) & (j >= 0) & (i < nx) & (j < ny)
        return np.where(ok, a[np.clip(i, 0, nx - 1), np.clip(j, 0, ny - 1)], 0)
    i, j = np.floor(u).astype(int), np.floor(v).astype(int)
    ok = (i >= 0) & (j >= 0) & (i < nx - 1) & (j < ny - 1)
    i, j = np.clip(i, 0, nx - 2), np.clip(j, 0, ny - 2)
    fu, fv = u - i, v - j
    out = (a[i, j] * (1 - fu) * (1 - fv) + a[i + 1, j] * fu * (1 - fv) + a[i, j + 1] * (1 - fu) * fv
           + a[i + 1, j + 1] * fu * fv)
    if field == "z":
        ok &= ~grid_discontinuous(spec, x, y)
    return np.where(ok, out, np.nan)






class Mesh:
    def __init__(self, v=None, f=None):
        self.v = np.zeros((0, 3)) if v is None else np.asarray(v, float)
        self.f = np.zeros((0, 3), int) if f is None else np.asarray(f, int)

    def __add__(self, other):
        return Mesh(np.vstack([self.v, other.v]), np.vstack([self.f, other.f + len(self.v)]))

    def welded(self, tol=1e-6):
        key = np.round(self.v / tol).astype(np.int64)
        _, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
        f = inv.reshape(-1)[self.f]
        keep = (f[:, 0] != f[:, 1]) & (f[:, 1] != f[:, 2]) & (f[:, 0] != f[:, 2])
        f = f[keep]
        used, f = np.unique(f, return_inverse=True)
        return Mesh(self.v[first][used], f.reshape(-1, 3))

    def volume(self):
        a, b, c = self.v[self.f[:, 0]], self.v[self.f[:, 1]], self.v[self.f[:, 2]]
        return float(np.einsum("ij,ij->i", a, np.cross(b, c)).sum() / 6)

    def check_closed(self):
        """Every directed edge must have exactly one opposite. Returns the number of bad edges."""
        e = np.concatenate([self.f[:, [0, 1]], self.f[:, [1, 2]], self.f[:, [2, 0]]])
        fwd = {tuple(x) for x in e.tolist()}
        bad = sum(1 for a, b in e.tolist() if (b, a) not in fwd)
        return bad + (len(e) - len(fwd))

    def bbox(self):
        return self.v.min(0), self.v.max(0)


def grid_solid(px, py, top, bottom, periodic_j=False, bottom_fan=None):
    """Solid between z=bottom and z=top over a structured grid px[i,j], py[i,j].

    Boundary walls run along i=0, i=ni-1 and (unless periodic in j) j=0, j=nj-1. Collapsed rows
    (a disk centre) weld into a point. `bottom_fan` = (x, y): triangulate the flat bottom as a fan
    from that point to the boundary instead of a full grid copy (valid for star-shaped domains)."""
    ni, nj = px.shape
    J = nj if periodic_j else nj - 1
    idx = lambda i, j: i * nj + (j % nj)
    vt = np.stack([px.ravel(), py.ravel(), top.ravel()], 1)
    vb = np.stack([px.ravel(), py.ravel(), np.full(px.size, bottom)], 1)
    n = len(vt)
    faces = []
    for i in range(ni - 1):
        for j in range(J):
            a, b, c, d = idx(i, j), idx(i + 1, j), idx(i + 1, j + 1), idx(i, j + 1)
            faces += [(a, b, c), (a, c, d)]
    top_f = np.array(faces)

    a, b, c = vt[top_f[:, 0]], vt[top_f[:, 1]], vt[top_f[:, 2]]
    if np.cross(b - a, c - a)[:, 2].sum() < 0:
        top_f = top_f[:, ::-1]

    loop = []
    if periodic_j:
        loop = [idx(ni - 1, j) for j in range(nj)]
    else:
        loop = ([idx(i, 0) for i in range(ni - 1)] + [idx(ni - 1, j) for j in range(nj - 1)]
                + [idx(i, nj - 1) for i in range(ni - 1, 0, -1)] + [idx(0, j) for j in range(nj - 1, 0, -1)])
    loop = np.array(loop)
    lx, ly = vt[loop, 0], vt[loop, 1]
    if (lx * np.roll(ly, -1) - np.roll(lx, -1) * ly).sum() < 0:
        loop = loop[::-1]
    walls = []
    for k in range(len(loop)):
        p, q = loop[k], loop[(k + 1) % len(loop)]
        walls += [(p, p + n, q + n), (p, q + n, q)]
    walls = np.array(walls)
    if bottom_fan is None:
        bot_f = top_f[:, ::-1] + n
        v = np.vstack([vt, vb])
    else:
        c = 2 * n
        bot_f = np.array([(loop[(k + 1) % len(loop)] + n, loop[k] + n, c) for k in range(len(loop))])
        v = np.vstack([vt, vb, [[bottom_fan[0], bottom_fan[1], bottom]]])
    m = Mesh(v, np.vstack([top_f, walls, bot_f])).welded()
    if m.volume() < 0:
        m.f = m.f[:, ::-1]
    return m


def disk_solid(cx, cy, r, nr, nt, top_fn, bottom=0.0, r0=0.0, phi0=0.0, phi1=None):
    """Height-field solid over a disk (r0 = 0, full turn) or an annular sector."""
    full = phi1 is None
    phis = np.linspace(phi0, phi0 + 2 * math.pi, nt, endpoint=False) if full else np.linspace(phi0, phi1, nt)
    rs = np.linspace(r0, r, nr + 1)
    R, P = np.meshgrid(rs, phis, indexing="ij")
    px, py = cx + R * np.cos(P), cy + R * np.sin(P)
    fan = (cx, cy) if full and r0 == 0 else None
    return grid_solid(px, py, top_fn(px, py), bottom, periodic_j=full, bottom_fan=fan)


def tri_solid(xy, tris, loop, top_fn, bottom=0.0):
    """Height-field solid over a 2-D triangulation `tris` of points `xy` with CCW boundary `loop`.
    The flat bottom is a fan from the centroid of the loop (star-shaped domains only)."""
    n = len(xy)
    top = np.column_stack([xy, top_fn(xy[:, 0], xy[:, 1])])
    loop = np.asarray(loop)
    vb = np.column_stack([xy[loop], np.full(len(loop), bottom)])
    c = xy[loop].mean(0)
    v = np.vstack([top, vb, [[c[0], c[1], bottom]]])
    tris = np.asarray(tris)
    a, b, cc = top[tris[:, 0]], top[tris[:, 1]], top[tris[:, 2]]
    if np.cross(b - a, cc - a)[:, 2].sum() < 0:
        tris = tris[:, ::-1]
    m = len(loop)
    faces = [tris]
    walls, fan = [], []
    for k in range(m):
        p, q = loop[k], loop[(k + 1) % m]
        bp, bq = n + k, n + (k + 1) % m
        walls += [(p, bp, bq), (p, bq, q)]
        fan.append((bq, bp, n + m))
    mesh = Mesh(v, np.vstack(faces + [np.array(walls), np.array(fan)])).welded()
    if mesh.volume() < 0:
        mesh.f = mesh.f[:, ::-1]
    return mesh


def hex_disk(cx, cy, r, rings):
    """Near-uniform triangulation of a disk: ring k has 6k points. Returns (xy, tris, loop)."""
    pts, start = [np.array([[cx, cy]])], [0]
    for k in range(1, rings + 1):
        t = 2 * math.pi * np.arange(6 * k) / (6 * k)
        rr = r * k / rings
        pts.append(np.column_stack([cx + rr * np.cos(t), cy + rr * np.sin(t)]))
        start.append(start[-1] + max(1, 6 * (k - 1)))
    xy = np.vstack(pts)
    tris = []
    for k in range(rings):
        na, nb = max(1, 6 * k), 6 * (k + 1)
        sa, sb = start[k], start[k + 1]
        i = j = 0
        while i < na or j < nb:

            ta = (i + 1) / na if na > 1 else 2.0
            tb = (j + 1) / nb
            if j < nb and (i >= na or tb <= ta):
                tris.append((sa + i % na, sb + j, sb + (j + 1) % nb))
                j += 1
            else:
                tris.append((sa + i % na, sb + j % nb, sa + (i + 1) % na))
                i += 1
    loop = np.arange(start[rings], start[rings] + 6 * rings)
    return xy, np.array(tris), loop


def rect_solid(x0, x1, y0, y1, nx, ny, top_fn, bottom=0.0):
    X, Y = np.meshgrid(np.linspace(x0, x1, nx + 1), np.linspace(y0, y1, ny + 1), indexing="ij")
    return grid_solid(X, Y, top_fn(X, Y), bottom, bottom_fan=((x0 + x1) / 2, (y0 + y1) / 2))


def cylinder(cx, cy, r, z0, z1, n=48):
    return disk_solid(cx, cy, r, 1, n, lambda x, y: np.full_like(x, z1), bottom=z0)


def _ear_clip(poly):
    """Triangulate a simple CCW polygon (k, 2). O(k^2), fine for profiles."""
    idx = list(range(len(poly)))
    tris = []

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    guard = 0
    while len(idx) > 3 and guard < 10 * len(poly) ** 2:
        guard += 1
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            a, b, c = poly[i0], poly[i1], poly[i2]
            if cross(a, b, c) <= 1e-12:
                continue
            inside = False
            for j in idx:
                if j in (i0, i1, i2):
                    continue
                p = poly[j]
                if cross(a, b, p) >= 0 and cross(b, c, p) >= 0 and cross(c, a, p) >= 0:
                    inside = True
                    break
            if not inside:
                tris.append((i0, i1, i2))
                idx.pop(k)
                break
        else:
            raise ValueError("ear clipping failed: polygon not simple")
    tris.append(tuple(idx))
    return np.array(tris)


def extrude_xz(profile, y0, y1):
    """Prism from a simple polygon in the XZ plane, swept along Y from y0 to y1."""
    prof = np.asarray(profile, float)
    if (prof[:, 0] * np.roll(prof[:, 1], -1) - np.roll(prof[:, 0], -1) * prof[:, 1]).sum() < 0:
        prof = prof[::-1]
    k = len(prof)
    tri = _ear_clip(prof)
    v0 = np.stack([prof[:, 0], np.full(k, y0), prof[:, 1]], 1)
    v1 = np.stack([prof[:, 0], np.full(k, y1), prof[:, 1]], 1)
    sides = []
    for i in range(k):
        j = (i + 1) % k
        sides += [(j, i, i + k), (j, i + k, j + k)]
    m = Mesh(np.vstack([v0, v1]), np.vstack([tri, tri[:, ::-1] + k, np.array(sides)]))
    if m.volume() < 0:
        m.f = m.f[:, ::-1]
    return m






def write_stl(mesh, path, name="part"):
    v, f = mesh.v, mesh.f
    a, b, c = v[f[:, 0]], v[f[:, 1]], v[f[:, 2]]
    nrm = np.cross(b - a, c - a)
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-30)
    rec = np.zeros(len(f), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    rec["n"], rec["v"] = nrm, np.stack([a, b, c], 1)
    with open(path, "wb") as fh:
        fh.write(name.encode()[:80].ljust(80, b"\0"))
        fh.write(struct.pack("<I", len(f)))
        fh.write(rec.tobytes())


def write_obj(mesh, path, comment=""):
    with open(path, "w", newline="\n") as fh:
        for line in comment.splitlines():
            fh.write("# %s\n" % line)
        num = lambda x: ("%.4f" % x).rstrip("0").rstrip(".") or "0"
        fh.writelines("v %s %s %s\n" % tuple(num(c) for c in p) for p in mesh.v)
        fh.writelines("f %d %d %d\n" % tuple(t + 1) for t in mesh.f)


def read_stl(path):
    data = open(path, "rb").read()
    if data[:5] == b"solid" and b"facet" in data[:1024]:
        pts = [list(map(float, l.split()[1:4])) for l in data.decode(errors="replace").splitlines()
               if l.strip().startswith("vertex")]
        v = np.array(pts)
    else:
        n = struct.unpack("<I", data[80:84])[0]
        rec = np.frombuffer(data[84:84 + 50 * n], dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
        v = rec["v"].reshape(-1, 3).astype(float)
    return Mesh(v, np.arange(len(v)).reshape(-1, 3))
