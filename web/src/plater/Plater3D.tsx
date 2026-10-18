import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { TransformControls } from 'three/examples/jsm/controls/TransformControls.js';
import { effectiveConfig, getState, printerPreset, setState, subscribeStore, updateInstance, updateObject, useStore, type State } from '../state/store';
import { buildBed } from './Bed';
import { bedBounds, instanceMatrix, outsideBed, parsePoints } from './geometry';

const SELECTED = new THREE.Color(0x00c1ae);
const OUTSIDE = new THREE.Color(0x4d9ed9);

export function filamentColor(s: State, extruder: number): string {
  const chosen = s.slotColours[extruder - 1];
  if (chosen) return chosen;
  const name = s.filamentSlots[extruder - 1] ?? s.filamentSlots[0];
  const cfg = name ? effectiveConfig(s, 'filament', extruder - 1) : {};
  const c = cfg.filament_colour ?? cfg.default_filament_colour;
  const v = Array.isArray(c) ? String(c[0]) : String(c ?? '');
  return /^#[0-9a-fA-F]{6}/.test(v) ? v.slice(0, 7) : '#00AE42';
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

    const camera = new THREE.PerspectiveCamera(35, 1, 1, 5000);
    camera.up.set(0, 0, 1);
    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = false;
    controls.mouseButtons = { LEFT: THREE.MOUSE.ROTATE, MIDDLE: THREE.MOUSE.PAN, RIGHT: THREE.MOUSE.PAN };
    controls.zoomToCursor = true;

    let frame = 0;
    const render = () => {
      if (frame) return;
      frame = requestAnimationFrame(() => {
        frame = 0;
        renderer.render(scene, camera);
      });
    };
    controls.addEventListener('change', render);

    let bed: THREE.Group | null = null;
    let bedKey = '';
    let area: THREE.Vector2[] = [];
    let height = 250;
    const fitCamera = () => {
      const { min, max, center } = bedBounds(area);
      const size = Math.max(max.x - min.x, max.y - min.y);
      controls.target.set(center.x, center.y, 0);
      camera.position.set(center.x, center.y - size * 1.55, size * 1.25);
      controls.update();
      render();
    };
    const syncBed = (s: State) => {
      const m = printerPreset(s);
      const key = `${m?.name}|${m?.config.printable_area}`;
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
        const c = new THREE.Color(color);
        if (state === 'selected') c.lerp(SELECTED, 0.55);
        if (state === 'outside') c.lerp(OUTSIDE, 0.7);
        m = new THREE.MeshStandardMaterial({ color: c, roughness: 0.65, metalness: 0.05, transparent: placeholder, opacity: placeholder ? 0.35 : 1 });
        materials.set(k, m);
      }
      return m;
    };
    const syncObjects = (s: State) => {
      const seen = new Set<number>();
      const out = outsideBed(s.objects, area, height);
      for (const o of s.objects) {
        const color = filamentColor(s, o.extruder);
        for (const inst of o.instances) {
          seen.add(inst.id);
          let mesh = meshes.get(inst.id);
          if (!mesh || mesh.geometry !== o.geometry) {
            if (mesh) scene.remove(mesh);
            mesh = new THREE.Mesh(o.geometry);
            mesh.matrixAutoUpdate = false;
            mesh.userData = { object: o.id, instance: inst.id };
            meshes.set(inst.id, mesh);
            scene.add(mesh);
          }
          mesh.matrix.copy(instanceMatrix(o, inst));
          mesh.matrixWorldNeedsUpdate = true;
          const sel = s.selected?.instance === inst.id;
          mesh.material = material(color, sel ? 'selected' : out.has(inst.id) ? 'outside' : 'normal', !!o.project?.placeholder);
        }
      }
      for (const [id, mesh] of meshes)
        if (!seen.has(id)) {
          scene.remove(mesh);
          meshes.delete(id);
        }
      syncGizmo(s);
      render();
    };

    const proxy = new THREE.Object3D();
    scene.add(proxy);
    const tc = new TransformControls(camera, renderer.domElement);
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
      if (tc.mode === 'translate') updateInstance(object, instance, { x: proxy.position.x, y: proxy.position.y });
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
      if (!o || !i || s.gizmo === 'none' || o.project) {
        tc.detach();
        return;
      }
      const mode = s.gizmo === 'move' ? 'translate' : s.gizmo;
      if (tc.mode !== mode) tc.setMode(mode);
      tc.showZ = mode !== 'translate';
      if (!dragStart) {
        const b = new THREE.Box3().setFromObject(meshes.get(i.id)!);
        proxy.position.set(i.x, i.y, mode === 'translate' ? 0 : (b.min.z + b.max.z) / 2);
        proxy.rotation.set(0, 0, 0);
        proxy.scale.set(1, 1, 1);
      }
      if (tc.object !== proxy) tc.attach(proxy);
    };
    gizmoRef.current = () => syncGizmo(getState());

    const ray = new THREE.Raycaster();
    const ndc = new THREE.Vector2();
    const pick = (ev: PointerEvent) => {
      const r = renderer.domElement.getBoundingClientRect();
      ndc.set(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
      ray.setFromCamera(ndc, camera);
      return ray.intersectObjects([...meshes.values()], false)[0];
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
      if (p && !o.project) drag = { object, instance, dx: i.x - p.x, dy: i.y - p.y, moved: false };
      controls.enabled = false;
      renderer.domElement.setPointerCapture(ev.pointerId);
    };
    const onMove = (ev: PointerEvent) => {
      if (!drag) return;
      pick(ev);
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

    const resize = () => {
      const w = el.clientWidth, h = el.clientHeight;
      renderer.setSize(w, h);
      camera.aspect = w / Math.max(1, h);
      camera.updateProjectionMatrix();
      render();
    };
    const ro = new ResizeObserver(resize);
    ro.observe(el);

    const onKey = (ev: KeyboardEvent) => {
      if ((ev.target as HTMLElement)?.closest('input,select,textarea')) return;
      const { min, max, center } = bedBounds(area);
      const size = Math.max(max.x - min.x, max.y - min.y);
      if (ev.key === '0') fitCamera();
      else if (ev.key === '1') camera.position.set(center.x, center.y - 0.001, size * 2.2);
      else if (ev.key === '3') camera.position.set(center.x, center.y - size * 2, size * 0.25);
      else return;
      controls.target.set(center.x, center.y, 0);
      controls.update();
      render();
    };
    window.addEventListener('keydown', onKey);

    let last: State | null = null;
    const sync = () => {
      const s = getState();
      if (last && s.objects === last.objects && s.selected === last.selected && s.printer === last.printer &&
          s.filamentSlots === last.filamentSlots && s.slotColours === last.slotColours && s.machines === last.machines && s.gizmo === last.gizmo && s.dirty === last.dirty) return;
      last = s;
      syncBed(s);
      syncObjects(s);
    };
    const unsub = subscribeStore(sync);
    sync();
    resize();

    return () => {
      unsub();
      ro.disconnect();
      window.removeEventListener('keydown', onKey);
      tc.dispose();
      controls.dispose();
      renderer.dispose();
      el.removeChild(renderer.domElement);
    };
  }, []);

  useEffect(() => gizmoRef.current(gizmo), [gizmo]);

  return <div ref={host} style={{ position: 'absolute', inset: 0 }} />;
}
