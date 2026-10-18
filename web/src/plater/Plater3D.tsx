import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { CameraViews } from './CameraViews';
import { TransformControls } from 'three/examples/jsm/controls/TransformControls.js';
import { getState, printerPreset, setState, subscribeStore, updateInstance, updateObject, useStore, type State } from '../state/store';
import { filamentColor } from './filamentColor';
import { buildBed } from './Bed';
import { raycastBvh, type Bvh } from './bvh';
import { instanceMatrix, outsideBed, parsePoints } from './geometry';

const OUTSIDE = new THREE.Color(0x4d9ed9);

function brightenColor(c: THREE.Color): THREE.Color {
  const hsl = c.getHSL({ h: 0, s: 0, l: 0 }, THREE.SRGBColorSpace);
  return c.clone().setHSL(hsl.h, hsl.s, Math.min(hsl.l + 0.25, 1), THREE.SRGBColorSpace);
}

export default function Plater3D() {
  const host = useRef<HTMLDivElement>(null);
  const gizmo = useStore((s) => s.gizmo);
  const gizmoRef = useRef<(g: State['gizmo']) => void>(() => {});

  useEffect(() => {
    const el = host.current!;
    const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setClearColor(0xe9e9e9);
    el.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    scene.add(new THREE.HemisphereLight(0xffffff, 0x8a8a8a, 1.6));
    const sun = new THREE.DirectionalLight(0xffffff, 1.6);
    sun.position.set(-0.4, -0.6, 1);
    scene.add(sun);
    const fill = new THREE.DirectionalLight(0xffffff, 0.5);
    fill.position.set(0.6, 0.8, 0.3);
    scene.add(fill);

    const views = new CameraViews(renderer.domElement, () => getState().tab === 'prepare', () => {
      tc.camera = views.camera;
      render();
    });
    const controls = views.controls;
    let frame = 0;
    const frames: { at: number; ms: number }[] = [];
    const render = () => {
      if (frame || !el.clientWidth) return;
      frame = requestAnimationFrame(() => {
        frame = 0;
        const t0 = performance.now();
        if (uploads.length) uploadStep();
        renderer.render(scene, views.camera);
        if (frames.length > 4096) frames.splice(0, 2048);
        frames.push({ at: t0, ms: performance.now() - t0 });
      });
    };

    const STAGED_BYTES = 8 << 20;
    const uploads: { geo: THREE.BufferGeometry; steps: (THREE.BufferAttribute | THREE.InterleavedBufferAttribute)[] }[] = [];
    const uploaded = new WeakSet<THREE.BufferGeometry>();
    const stageGeo = new THREE.BufferGeometry();
    stageGeo.setDrawRange(0, 0);
    stageGeo.boundingSphere = new THREE.Sphere();
    const dummy = new THREE.BufferAttribute(new Float32Array(3), 3);
    stageGeo.setAttribute('position', dummy);
    const stage = new THREE.Mesh(stageGeo, new THREE.MeshBasicMaterial());
    stage.frustumCulled = false;
    const stageScene = new THREE.Scene().add(stage);
    void renderer.compileAsync(stageScene, views.camera);
    const needsUpload = (geo: THREE.BufferGeometry) => {
      if (uploaded.has(geo)) return false;
      const steps = [...Object.values(geo.attributes), ...(geo.index ? [geo.index] : [])];
      if (steps.reduce((n, a) => n + a.array.byteLength, 0) < STAGED_BYTES) return false;
      if (!uploads.some((u) => u.geo === geo)) uploads.push({ geo, steps });
      return true;
    };
    const uploadStep = () => {
      const u = uploads[0];
      const a = u.steps.shift()!;
      for (const k of Object.keys(stageGeo.attributes)) stageGeo.deleteAttribute(k);
      stageGeo.setIndex(null);
      stageGeo.setAttribute('position', a === u.geo.index ? dummy : (a as THREE.BufferAttribute));
      if (a === u.geo.index) stageGeo.setIndex(a as THREE.BufferAttribute);
      renderer.render(stageScene, views.camera);
      stageGeo.setAttribute('position', dummy);
      stageGeo.setIndex(null);
      if (!u.steps.length) {
        uploads.shift();
        uploaded.add(u.geo);
        for (const m of meshes.values()) if (m.geometry === u.geo) m.visible = true;
      }
      render();
    };
    controls.addEventListener('change', render);

    let bed: THREE.Group | null = null;
    let bedKey = '';
    let area: THREE.Vector2[] = [];
    let height = 250;
    const fitCamera = () => views.setBed(area);
    const syncBed = (s: State) => {
      const m = printerPreset(s);
      const key = `${m?.name}|${m?.config.printable_area}|${m?.config.bed_custom_texture}|${m?.config.bed_custom_model}`;
      if (key === bedKey) return;
      bedKey = key;
      if (bed) scene.remove(bed);
      bed = buildBed(m, render);
      scene.add(bed);
      area = parsePoints(m?.config.printable_area);
      height = Number(m?.config.printable_height ?? 250) || 250;
      fitCamera();
    };

    const meshes = new Map<number, THREE.Mesh>();
    const materials = new Map<string, THREE.MeshStandardMaterial>();
    const material = (color: string, state: 'normal' | 'selected' | 'outside', placeholder = false) => {
      const k = color + state + placeholder;
      let m = materials.get(k);
      if (!m) {
        let c = new THREE.Color(color);
        if (state === 'selected') c = brightenColor(c);
        if (state === 'outside') c.lerp(OUTSIDE, 0.7);
        m = new THREE.MeshStandardMaterial({ color: c, roughness: 0.65, metalness: 0.05, transparent: placeholder, opacity: placeholder ? 0.35 : 1 });
        materials.set(k, m);
      }
      return m;
    };
    {
      const warm = new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1), material('#00AE42', 'normal'));
      scene.add(warm);
      void renderer.compileAsync(warm, views.camera, scene).finally(() => {
        scene.remove(warm);
        warm.geometry.dispose();
      });
    }
    let liveGeometries = new Set<THREE.BufferGeometry>();
    let outside = new Set<number>();
    let hovered: number | null = null;
    const tint = (s: State) => {
      for (const o of s.objects) {
        const color = filamentColor(s, o.extruder);
        for (const inst of o.instances) {
          const mesh = meshes.get(inst.id);
          if (!mesh) continue;
          const state = outside.has(inst.id) ? 'outside' : s.selected?.instance === inst.id ? 'selected' : 'normal';
          mesh.material = material(inst.printable === false ? '#999999' : color, inst.printable === false ? 'normal' : state, !!o.project?.placeholder);
        }
      }
    };
    const syncObjects = (s: State) => {
      const seen = new Set<number>();
      outside = outsideBed(s.objects, area, height);
      for (const o of s.objects) {
        for (const inst of o.instances) {
          seen.add(inst.id);
          let mesh = meshes.get(inst.id);
          if (!mesh || mesh.geometry !== o.geometry) {
            if (mesh) scene.remove(mesh);
            mesh = new THREE.Mesh(o.geometry);
            mesh.visible = !needsUpload(o.geometry);
            mesh.matrixAutoUpdate = false;
            mesh.userData = { object: o.id, instance: inst.id };
            meshes.set(inst.id, mesh);
            scene.add(mesh);
          }
          mesh.matrix.copy(instanceMatrix(o, inst));
          mesh.matrixWorldNeedsUpdate = true;
        }
      }
      if (hovered !== null && !seen.has(hovered)) hovered = null;
      tint(s);
      for (const [id, mesh] of meshes)
        if (!seen.has(id)) {
          scene.remove(mesh);
          meshes.delete(id);
        }
      const live = new Set(s.objects.map((o) => o.geometry));
      for (const g of liveGeometries) if (!live.has(g)) g.dispose();
      liveGeometries = live;
      syncGizmo(s);
      render();
    };

    const proxy = new THREE.Object3D();
    scene.add(proxy);
    const tc = new TransformControls(views.camera, renderer.domElement);
    tc.setSize(0.9);
    const tcHelper = tc.getHelper();
    scene.add(tcHelper);
    tc.addEventListener('change', render);
    let dragStart: { x: number; y: number; rotX: number; rotY: number; rotZ: number; scale: [number, number, number] } | null = null;
    tc.addEventListener('dragging-changed', (e) => {
      controls.enabled = !(e as unknown as { value: boolean }).value;
      const s = getState();
      const o = s.objects.find((ob) => ob.id === s.selected?.object);
      const i = o?.instances.find((ii) => ii.id === s.selected?.instance);
      if ((e as unknown as { value: boolean }).value && o && i) {
        dragStart = { x: i.x, y: i.y, rotX: o.rotX, rotY: o.rotY, rotZ: i.rotZ, scale: [...o.scale] };
        proxy.rotation.set(0, 0, 0);
        proxy.scale.set(1, 1, 1);
      } else dragStart = null;
    });
    tc.addEventListener('objectChange', () => {
      const s = getState();
      if (!dragStart || !s.selected) return;
      const { object, instance } = s.selected;
      if (tc.mode === 'translate') updateInstance(object, instance, { x: proxy.position.x, y: proxy.position.y, z: proxy.position.z });
      else if (tc.mode === 'rotate') {
        const deg = 180 / Math.PI;
        updateObject(object, { rotX: dragStart.rotX + proxy.rotation.x * deg, rotY: dragStart.rotY + proxy.rotation.y * deg });
        updateInstance(object, instance, { rotZ: dragStart.rotZ + proxy.rotation.z * deg });
      } else if (tc.mode === 'scale') {
        const k = Math.max(0.01, (proxy.scale.x + proxy.scale.y + proxy.scale.z) / 3);
        updateObject(object, { scale: dragStart.scale.map((v) => v * k) as [number, number, number] });
      }
    });
    const syncGizmo = (s: State) => {
      const o = s.objects.find((ob) => ob.id === s.selected?.object);
      const i = o?.instances.find((ii) => ii.id === s.selected?.instance);
      if (!o || !i || s.gizmo === 'none' || o.project?.placeholder) {
        tc.detach();
        return;
      }
      const mode = s.gizmo === 'move' ? 'translate' : s.gizmo;
      if (tc.mode !== mode) tc.setMode(mode);
      tc.showZ = true;
      if (!dragStart) {
        const b = new THREE.Box3().setFromObject(meshes.get(i.id)!);
        proxy.position.set(i.x, i.y, mode === 'translate' ? (i.z ?? 0) : (b.min.z + b.max.z) / 2);
        proxy.rotation.set(0, 0, 0);
        proxy.scale.set(1, 1, 1);
      }
      if (tc.object !== proxy) tc.attach(proxy);
    };
    gizmoRef.current = () => syncGizmo(getState());

    const ray = new THREE.Raycaster();
    const ndc = new THREE.Vector2();
    const setRay = (ev: { clientX: number; clientY: number }) => {
      const r = renderer.domElement.getBoundingClientRect();
      ndc.set(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
      ray.setFromCamera(ndc, views.camera);
    };
    const EXACT_TRIANGLES = 100_000;
    const box = new THREE.Box3();
    const entry = new THREE.Vector3();
    const inverse = new THREE.Matrix4();
    const linear = new THREE.Matrix3();
    const origin = new THREE.Vector3(), direction = new THREE.Vector3();
    const pick = (ev: { clientX: number; clientY: number }): { object: THREE.Object3D; distance: number } | undefined => {
      setRay(ev);
      const hits: { mesh: THREE.Mesh; distance: number }[] = [];
      for (const mesh of meshes.values()) {
        box.copy(mesh.geometry.boundingBox!).applyMatrix4(mesh.matrixWorld);
        if (ray.ray.intersectBox(box, entry)) hits.push({ mesh, distance: entry.distanceTo(ray.ray.origin) });
      }
      hits.sort((a, b) => a.distance - b.distance);
      let best: { object: THREE.Object3D; distance: number } | undefined;
      for (const { mesh, distance } of hits) {
        if (best && distance >= best.distance) break;
        const g = mesh.geometry;
        const bvh = g.userData.bvh as Bvh | undefined;
        let hit: { object: THREE.Object3D; distance: number } | undefined;
        if (bvh) {
          inverse.copy(mesh.matrixWorld).invert();
          origin.copy(ray.ray.origin).applyMatrix4(inverse);
          direction.copy(ray.ray.direction).applyMatrix3(linear.setFromMatrix4(inverse));
          const t = raycastBvh(bvh, g.attributes.position.array, g.index!.array, origin.toArray(), direction.toArray(), best?.distance);
          if (t !== Infinity) hit = { object: mesh, distance: t };
        } else {
          const triangles = (g.index ? g.index.count : g.attributes.position.count) / 3;
          hit = triangles > EXACT_TRIANGLES ? { object: mesh, distance } : ray.intersectObject(mesh, false)[0];
        }
        if (hit && (!best || hit.distance < best.distance)) best = hit;
      }
      return best;
    };
    const bedPoint = () => {
      const p = new THREE.Vector3();
      return ray.ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0, 0, 1), 0), p) ? p : null;
    };
    let drag: { object: number; instance: number; dx: number; dy: number; moved: boolean } | null = null;
    let downAt: { x: number; y: number } | null = null;
    const onDown = (ev: PointerEvent) => {
      if (ev.button !== 0 || (tc.dragging || (tc as unknown as { axis: string | null }).axis)) return;
      downAt = { x: ev.clientX, y: ev.clientY };
      const hit = pick(ev);
      if (!hit) return;
      const { object, instance } = hit.object.userData as { object: number; instance: number };
      setState({ selected: { object, instance } });
      const p = bedPoint();
      const o = getState().objects.find((ob) => ob.id === object)!;
      const i = o.instances.find((ii) => ii.id === instance)!;
      if (p && !o.project?.placeholder) drag = { object, instance, dx: i.x - p.x, dy: i.y - p.y, moved: false };
      controls.enabled = false;
      renderer.domElement.setPointerCapture(ev.pointerId);
    };
    const onMove = (ev: PointerEvent) => {
      if (!drag) return;
      setRay(ev);
      const p = bedPoint();
      if (!p) return;
      drag.moved = true;
      updateInstance(drag.object, drag.instance, { x: p.x + drag.dx, y: p.y + drag.dy });
    };
    const onUp = (ev: PointerEvent) => {
      if (!drag && downAt && Math.hypot(ev.clientX - downAt.x, ev.clientY - downAt.y) < 4 && !tc.dragging && !pick(ev))
        setState({ selected: null });
      drag = null;
      downAt = null;
      controls.enabled = true;
    };
    renderer.domElement.addEventListener('pointerdown', onDown);
    renderer.domElement.addEventListener('pointermove', onMove);
    renderer.domElement.addEventListener('pointerup', onUp);

    let hoverAt: { clientX: number; clientY: number } | null = null;
    let hoverFrame = 0;
    let orbiting = false;
    const hoverMs: number[] = [];
    const setHover = (id: number | null) => {
      if (id === hovered) return;
      hovered = id;
      tint(getState());
      render();
    };
    const hoverPass = () => {
      hoverFrame = 0;
      if (!hoverAt || orbiting) return;
      const t0 = performance.now();
      const axis = (tc as unknown as { axis: string | null }).axis;
      const hit = axis ? undefined : pick(hoverAt);
      if (hoverMs.length > 4096) hoverMs.splice(0, 2048);
      hoverMs.push(performance.now() - t0);
      setHover(hit ? (hit.object.userData as { instance: number }).instance : null);
    };
    const onHover = (ev: PointerEvent) => {
      if (ev.buttons || orbiting || tc.dragging) {
        hoverAt = null;
        setHover(null);
        return;
      }
      hoverAt = { clientX: ev.clientX, clientY: ev.clientY };
      if (!hoverFrame) hoverFrame = requestAnimationFrame(hoverPass);
    };
    const onLeave = () => {
      hoverAt = null;
      setHover(null);
    };
    controls.addEventListener('start', () => {
      orbiting = true;
      onLeave();
    });
    controls.addEventListener('end', () => (orbiting = false));
    renderer.domElement.addEventListener('pointermove', onHover);
    renderer.domElement.addEventListener('pointerleave', onLeave);

    const resize = () => {
      const w = el.clientWidth, h = el.clientHeight;
      if (!w || !h) return;
      const size = renderer.getSize(new THREE.Vector2());
      if (size.x !== w || size.y !== h) renderer.setSize(w, h);
      views.resize(w, h);
      render();
    };
    const ro = new ResizeObserver(resize);
    ro.observe(el);

    let last: State | null = null;
    const sync = () => {
      const s = getState();
      if (last && s.objects === last.objects && s.selected === last.selected && s.printer === last.printer &&
          s.filamentSlots === last.filamentSlots && s.slotColours === last.slotColours && s.machines === last.machines && s.gizmo === last.gizmo && s.dirty === last.dirty) return;
      last = s;
      syncBed(s);
      syncObjects(s);
    };
    (window as unknown as { __plater: unknown }).__plater = { scene, get camera() { return views.camera; }, controls, tc, renderer, meshes, frames, hoverMs, hovered: () => hovered, pick, render, getState, setState };
    const unsub = subscribeStore(sync);
    sync();
    resize();

    return () => {
      unsub();
      cancelAnimationFrame(hoverFrame);
      ro.disconnect();
      views.dispose();
      tc.dispose();

      renderer.dispose();
      el.removeChild(renderer.domElement);
    };
  }, []);

  useEffect(() => gizmoRef.current(gizmo), [gizmo]);

  return <div ref={host} style={{ position: 'absolute', inset: 0 }} />;
}
