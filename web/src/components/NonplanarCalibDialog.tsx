import { useEffect, useState } from 'react';
import { effectiveConfig, getState, markStale, presetEdits, setState, toast, useStore } from '../state/store';
import type { ConfigValue } from '../api/types';
import { undoable } from '../state/history';
import { headLabel, loadHead, loadHeadIndex, type Head } from '../heads';
import { Modal, ModalTitle } from './Modal';
import { runCalibration } from './CalibDialog';

type ClearanceStep = { slope: number; relief: number };
const STEPS: ClearanceStep[] = [[12, 0.8], [15, 1.2], [18, 1.8], [21, 2.6], [24, 3.6], [27, 4.6], [30, 5.8], [32, 7.2]].map(([slope, relief]) => ({ slope, relief }));
const TILT_SLOPES = [0, 10, 20, 30, 40];
const TILT_MULTIPLIERS = [0.9, 0.95, 1, 1.05, 1.1];
const TOP_SPEEDS = [60, 80, 100];
const TOP_FANS = [0, 50, 100];
const CONE_APEX_RADIUS = 1.5;

const rad = (deg: number) => (deg * Math.PI) / 180;
const footRadius = (s: ClearanceStep) => s.relief / Math.tan(rad(s.slope)) + 0.5 * CONE_APEX_RADIUS;
function coneHeight(s: ClearanceStep, r: number) {
  const t = Math.tan(rad(s.slope)), foot = footRadius(s);
  if (r >= CONE_APEX_RADIUS) return Math.max(0, (foot - r) * t);
  return (foot - CONE_APEX_RADIUS) * t + (t * (CONE_APEX_RADIUS * CONE_APEX_RADIUS - r * r)) / (2 * CONE_APEX_RADIUS);
}

function stepClearance(head: Head, s: ClearanceStep, gap: number) {
  const foot = footRadius(s);
  let worst = Infinity;
  for (let ring = 0; ring <= 8; ring++) {
    const rho = (foot * ring) / 8, tip = coneHeight(s, rho);
    for (let k = 0; k < 72; k++) {
      const phi = (2 * Math.PI * k) / 72, ax = rho * Math.cos(phi), ay = rho * Math.sin(phi);
      for (const box of head.boxes) {
        const grow = box.nozzle ? 0 : head.lateral_margin;
        const nx = box.infinite_x ? ax : Math.min(Math.max(ax, box.min[0] - grow), box.max[0] + grow);
        const ny = box.infinite_y ? ay : Math.min(Math.max(ay, box.min[1] - grow), box.max[1] + grow);
        let z = box.min[2];
        if (box.nozzle) {
          const hx = Math.max(Math.abs(box.min[0]), Math.abs(box.max[0])), hy = Math.max(Math.abs(box.min[1]), Math.abs(box.max[1]));
          if (Math.hypot(hx, hy) <= head.tip_allowance.radius) z = Math.max(z, head.tip_allowance.height);
          if (z >= box.max[2]) continue;
        } else z -= head.margin;
        worst = Math.min(worst, gap + z - (coneHeight(s, Math.hypot(nx - ax, ny - ay)) - tip));
      }
    }
  }
  return worst;
}

const stepText = (k: number) => (k === 0 ? 'None (cone 1 already touched)' : `Cone ${k}: ${STEPS[k - 1].slope}°, ${STEPS[k - 1].relief} mm`);
const firstTouchText = (k: number) => (k > STEPS.length ? 'no cone' : `cone ${k}`);
const logValue = (log: string[] | undefined, key: string) => log?.find((l) => l.startsWith(`${key} `))?.slice(key.length + 1);
const list = (v: ConfigValue | undefined) => (Array.isArray(v) ? v : String(v ?? '').split(/[,;]/)).map((x) => Number(x));
const fmt = (x: number) => String(Math.round(x * 1000) / 1000);

function writeProcess(name: string, keys: Record<string, ConfigValue>) {
  undoable(name, () => setState((s) => ({ dirty: { ...s.dirty, process: { ...presetEdits(s, 'process'), ...keys } }, job: markStale(s.job) })));
}

const memo = { page: 0, lastClean: Math.floor(STEPS.length / 2), tilt: TILT_SLOPES.map(() => Math.floor(TILT_MULTIPLIERS.length / 2)), speed: TOP_SPEEDS.length - 1, fan: 0, verifyHead: '', tiltCurve: null as number[] | null };

function Choice({ label, items, value, onChange }: { label: string; items: string[]; value: number; onChange(i: number): void }) {
  return (
    <label className="prefs-row">
      <span style={{ minWidth: 170 }}>{label}</span>
      <select className="combo" style={{ width: 300 }} value={value} onChange={(e) => onChange(Number(e.target.value))}>
        {items.map((it, i) => <option key={i} value={i}>{it}</option>)}
      </select>
    </label>
  );
}

const PAGES = ['Head clearance', 'Tilted-bead flow', 'Curved-top quality', 'Check head model'];

export default function NonplanarCalibDialog() {
  const s = useStore((st) => st);
  const machine = effectiveConfig(s, 'machine'), process = effectiveConfig(s, 'process');
  const headId = String(machine.nonplanar_head ?? '');
  const layerHeight = Number(process.layer_height) || 0.2;
  const [page, setPage] = useState(memo.page);
  const [lastClean, setLastClean] = useState(memo.lastClean);
  const [tilt, setTilt] = useState(memo.tilt);
  const [speed, setSpeed] = useState(memo.speed);
  const [fan, setFan] = useState(memo.fan);
  const [verifyHead, setVerifyHead] = useState(memo.verifyHead || headId);
  const [head, setHead] = useState<Head | null>(null);
  const [result, setResult] = useState<Record<number, string>>({});
  Object.assign(memo, { page, lastClean, tilt, speed, fan, verifyHead });
  useEffect(() => void loadHeadIndex().catch(() => undefined), []);
  useEffect(() => void loadHead(headId).then(setHead), [headId]);
  const close = () => setState({ calibDialog: null });
  const generate = (test: 'clearance' | 'tilt-flow' | 'curved-top' | 'verifier') => {
    const curve = list(process.nonplanar_tilt_flow);
    void runCalibration(`nonplanar-${test}`, '', test === 'verifier' ? verifyHead : undefined).then((started) => {
      if (started && test === 'tilt-flow') memo.tiltCurve = curve;
    });
  };
  const writeTilt = () => {
    let base = memo.tiltCurve ?? list(process.nonplanar_tilt_flow);
    if (base.length !== TILT_SLOPES.length || base.some((x) => !Number.isFinite(x))) base = TILT_SLOPES.map(() => 1);
    const curve = tilt.map((p, i) => Math.round(Math.min(1.5, Math.max(0.5, base[i] * TILT_MULTIPLIERS[p])) * 1000) / 1000);
    const value = curve.map(fmt).join(',');
    writeProcess('Change setting "Curved top flow by slope"', { nonplanar_tilt_flow: value });
    setResult({ ...result, 1: `Tilt flow ${value} written to the process preset. Save it to keep it.` });
  };
  const writeTop = () => {
    writeProcess('Non-planar calibration - Curved-top quality', { nonplanar_top_speed_factor: `${TOP_SPEEDS[speed]}%`, nonplanar_top_fan_boost: `${TOP_FANS[fan]}%` });
    setResult({ ...result, 2: `Curved top at ${TOP_SPEEDS[speed]}% speed with ${TOP_FANS[fan]}% fan boost written to the process preset. Save it to keep it.` });
  };
  const check = () => {
    const job = getState().job, log = job.calibLog;
    const verified = Number(logValue(log, 'calib_verifier_first_touch')), model = Number(logValue(log, 'calib_model_first_touch'));
    if (job.calib !== 'nonplanar-verifier' || job.state !== 'done' || job.stale || !Number.isFinite(verified) || !Number.isFinite(model))
      return toast('Generate the clearance plate and slice it first.');
    const expected = verifyHead || 'generic-conservative';
    if (logValue(log, 'calib_head') !== expected) return toast(`The sliced plate was checked with ${logValue(log, 'calib_head')}: generate it again with ${expected}.`);
    const measured = lastClean + 1;
    setResult({ ...result, 3: `First touch: clearance check ${firstTouchText(verified)}, head model ${firstTouchText(model)}, your print ${firstTouchText(measured)}.\n` +
      (verified === measured ? 'The clearance check agrees with your print.' : 'The clearance check disagrees with your print: calibrate the head on the Head clearance page.') });
  };
  const sliced = s.job.calib === 'nonplanar-clearance' && logValue(s.job.calibLog, 'calib_head') === (headId || 'generic-conservative') ? s.job.calibLog!.filter((l) => l.startsWith('calib_step ')) : [];
  const verdict = (k: number) => {
    const c = head ? stepClearance(head, STEPS[k], layerHeight) : Number(sliced[k]?.split(' ')[4]);
    return Number.isFinite(c) ? ` — ${c >= 0 ? 'head model: clean' : 'head model: touches'}` : '';
  };
  const steps = [`Head ${headId || 'generic-conservative'}, layer height ${layerHeight} mm:`, ...STEPS.map((_, k) => stepText(k + 1) + verdict(k))].join('\n');
  const heads = (s.heads ?? []).map((h) => h.id).filter((id) => id !== 'calibration-probe');
  const intro = [
    "Prints eight cones, each steeper and taller than the one before. The slicer prints every cone curved, whatever the printer's head, so where the nozzle, sock, fan duct or gantry passes over earlier material it scuffs that cone. Pick the last cone whose flank and top are clean.",
    "Prints five rows of curved patches at 0, 10, 20, 30 and 40° slope; along each row the flow steps from 0.90 to 1.10 of the current value. Each object's name gives its slope and flow. For each slope pick the patch whose top is closed without ridges, as in the flow rate calibration. Write tilt flow sets Curved top flow by slope in the process preset.",
    'Five dome and saddle pairs in a cross. The front row prints the curved top at 60, 80 and 100% of the top surface speed (left to right); the right column at 0, 50 and 100% extra fan toward the overhang fan speed (front to back), at full speed. Pick the best speed in the front row and the best fan in the right column. Both settings are written to the process preset.',
    'Runs the clearance check with the selected head over the sliced clearance plate. The first cone it finds the head touching should be the one after the last clean cone on your print. Generate the plate, slice it, then check.',
  ][page];
  return (
    <Modal className="np-calib-dialog" label="Non-planar calibration" onClose={close}>
      <ModalTitle title="Non-planar calibration" onClose={close} />
      <div className="page-bar" role="tablist">
        {PAGES.map((p, i) => <button key={p} role="tab" aria-selected={i === page} className={`page-tab ${i === page ? 'active' : ''}`} onClick={() => setPage(i)}>{p}</button>)}
      </div>
      <div className="prefs-body" role="tabpanel" aria-label={PAGES[page]} style={{ display: 'flex', flexDirection: 'column', gap: 8, alignItems: 'flex-start' }}>
        <div>{intro}</div>
        {page === 0 && <>
          <button className="btn" onClick={() => generate('clearance')}>Generate clearance plate</button>
          <div style={{ whiteSpace: 'pre-line' }}>{steps}</div>
          <Choice label="Last clean cone" items={STEPS.map((_, k) => stepText(k)).concat(stepText(STEPS.length))} value={lastClean} onChange={setLastClean} />
        </>}
        {page === 1 && <>
          <button className="btn" onClick={() => generate('tilt-flow')}>Generate tilted-bead flow plate</button>
          {TILT_SLOPES.map((slope, i) => (
            <Choice key={slope} label={`Best patch at ${slope}°`} items={TILT_MULTIPLIERS.map((m, j) => `Patch ${j + 1} (×${m.toFixed(2)})`)} value={tilt[i]}
              onChange={(v) => setTilt(tilt.map((x, j) => (j === i ? v : x)))} />
          ))}
          <button className="btn" onClick={writeTilt}>Write tilt flow</button>
        </>}
        {page === 2 && <>
          <button className="btn" onClick={() => generate('curved-top')}>Generate curved-top plate</button>
          <Choice label="Best speed (front row)" items={TOP_SPEEDS.map((x) => `${x}% of top surface speed`)} value={speed} onChange={setSpeed} />
          <Choice label="Best fan (right column)" items={TOP_FANS.map((x) => `${x}% fan boost`)} value={fan} onChange={setFan} />
          <button className="btn" onClick={writeTop}>Write curved-top settings</button>
        </>}
        {page === 3 && <>
          <button className="btn" onClick={() => generate('verifier')}>Generate clearance plate</button>
          <label className="prefs-row">
            <span style={{ minWidth: 170 }}>Head</span>
            <select className="combo" style={{ width: 300 }} value={verifyHead} onChange={(e) => setVerifyHead(e.target.value)}>
              {(heads.includes(verifyHead) ? heads : [verifyHead, ...heads]).map((id) => <option key={id} value={id}>{id ? headLabel(id) : headLabel('')}</option>)}
            </select>
          </label>
          <button className="btn" onClick={check}>Check sliced plate</button>
        </>}
        {result[page] && <div style={{ whiteSpace: 'pre-line' }}>{result[page]}</div>}
      </div>
      <div className="modal-buttons">
        <button className="btn primary" data-autofocus onClick={close}>OK</button>
      </div>
    </Modal>
  );
}
