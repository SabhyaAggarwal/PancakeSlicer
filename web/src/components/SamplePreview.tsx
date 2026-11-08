import { useEffect, useRef, useState } from 'react';
import * as THREE from 'three';
import { DRACOLoader } from 'three/examples/jsm/loaders/DRACOLoader.js';

const W = 220, H = 160;
let loader: DRACOLoader | null = null;
let renderer: THREE.WebGLRenderer | null = null;
const geometries = new Map<string, Promise<THREE.BufferGeometry>>();

function geometryOf(url: string) {
  if (!geometries.has(url)) {
    loader ??= new DRACOLoader().setDecoderPath(`${import.meta.env.BASE_URL}draco/`);
    const p = loader.loadAsync(url).then((g) => { g.computeVertexNormals(); g.computeBoundingSphere(); return g; });
    p.catch(() => geometries.delete(url));
    geometries.set(url, p);
  }
  return geometries.get(url)!;
}

const ink = () => getComputedStyle(document.documentElement).getPropertyValue('--accent').trim() || '#ffc43d';

export default function SamplePreview({ url }: { url: string }) {
  const host = useRef<HTMLDivElement>(null);
  const [ready, setReady] = useState(false);
  useEffect(() => {
    let frame = 0, live = true;
    const still = matchMedia('(prefers-reduced-motion: reduce)').matches;
    void geometryOf(url).then((g) => {
      if (!live || !host.current) return;
      renderer ??= new THREE.WebGLRenderer({ antialias: true, alpha: true });
      renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
      renderer.setSize(W, H);
      host.current.appendChild(renderer.domElement);
      const scene = new THREE.Scene();
      scene.add(new THREE.HemisphereLight(0xffffff, 0x404040, 2.2));
      const key = new THREE.DirectionalLight(0xffffff, 1.6);
      key.position.set(1, -1.5, 2);
      scene.add(key);
      const { center, radius } = g.boundingSphere!;
      const mesh = new THREE.Mesh(g, new THREE.MeshStandardMaterial({ color: ink(), roughness: 0.55, metalness: 0 }));
      mesh.position.set(-center.x, -center.y, -center.z);
      const turn = new THREE.Group();
      turn.add(mesh);
      scene.add(turn);
      const camera = new THREE.PerspectiveCamera(30, W / H, radius / 10, radius * 20);
      camera.up.set(0, 0, 1);
      const d = radius / Math.sin(THREE.MathUtils.degToRad(15)) * 1.05;
      camera.position.set(0, -d * Math.cos(0.55), d * Math.sin(0.55));
      camera.lookAt(0, 0, 0);
      setReady(true);
      const t0 = performance.now();
      const draw = () => {
        turn.rotation.z = still ? 0.6 : 0.6 + (performance.now() - t0) / 2500;
        renderer!.render(scene, camera);
        if (!still) frame = requestAnimationFrame(draw);
      };
      draw();
    }).catch(() => {});
    return () => { live = false; cancelAnimationFrame(frame); renderer?.domElement.remove(); };
  }, [url]);
  return <div ref={host} className="sample-preview" data-ready={ready || undefined} style={{ width: W, height: H }} />;
}
