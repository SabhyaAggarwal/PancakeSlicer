import { useEffect, useRef, useState } from 'react';
import penUrl from '../../../docs/nonplanar/corpus/everyday/pen_holder.stl?url';
import knobUrl from '../../../tests/data/nonplanar/corpus/w_knob_knurled.stl?url';
import hookUrl from '../samples/shelf_hook.stl?url';
import { choosePrinter, importFiles, openFileDialog, openProjectDialog } from '../actions';
import { DEMO_MODELS, getState, printerPreset, setState, subscribeStore, toast, useStore } from '../state/store';
import { Icon } from './Icon';
import './first-run.css';
import { Modal, ModalTitle } from './Modal';

const SETUP_KEY = 'orca.setup';
const SAMPLES = [
  { url: penUrl, file: 'pen_holder.stl', label: 'Pen holder', tip: 'No unneeded support: Auto matches the plain print' },
  { url: knobUrl, file: 'knob.stl', label: 'Knob', tip: 'An everyday part with a rounded, knurled top' },
  { url: hookUrl, file: 'shelf_hook.stl', label: 'Shelf hook', tip: 'A 90° ledge that prints without support' },
];

const nozzleOf = (v: unknown) => Number(Array.isArray(v) ? v[0] : v);

async function addSample(url: string, file: string) {
  setState({ loading: `Loading ${file}` });
  try {
    const r = await fetch(url);
    if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
    await importFiles([new File([await r.blob()], file)]);
  } catch (e) {
    if (getState().loading === `Loading ${file}`) setState({ loading: null });
    toast(`Sample download failed: ${e instanceof Error ? e.message : e}`);
  }
}

function PrinterChoice() {
  const machines = useStore((s) => s.machines);
  const current = useStore(printerPreset);
  const rank = (m: string) => DEMO_MODELS.includes(m) ? DEMO_MODELS.indexOf(m) : DEMO_MODELS.length;
  const models = [...new Set(machines.map((m) => m.model).filter((m): m is string => !!m))].sort((a, b) => rank(a) - rank(b));
  const nozzles = machines.filter((m) => m.model === current?.model)
    .map((m) => ({ name: m.name, d: nozzleOf(m.config.nozzle_diameter) }))
    .sort((a, b) => a.d - b.d);
  const pick = (model: string) => {
    const same = machines.filter((m) => m.model === model);
    const want = nozzleOf(current?.config.nozzle_diameter) || 0.4;
    const next = same.find((m) => nozzleOf(m.config.nozzle_diameter) === want) ?? same.find((m) => nozzleOf(m.config.nozzle_diameter) === 0.4) ?? same[0];
    if (next) choosePrinter(next.name);
  };
  return (
    <div className="fr-printer">
      <div className="label-caps fr-label">Printer</div>
      <div className="fr-models" role="radiogroup" aria-label="Printer">
        {models.map((model) => {
          const vendor = machines.find((m) => m.model === model)!.vendor;
          return (
            <button key={model} type="button" role="radio" aria-checked={current?.model === model}
              className={`fr-model ${current?.model === model ? 'active' : ''}`} onClick={() => pick(model)}>
              <img src={`/profiles/${vendor}/${encodeURIComponent(model)}_cover.png`} alt="" onError={(e) => ((e.target as HTMLImageElement).src = '/images/printer_placeholder.png')} />
              <span>{model.replace(/^Bambu Lab /, '')}</span>
            </button>
          );
        })}
      </div>
      <div className="fr-nozzles">
        <span className="label-caps">Nozzle</span>
        <div className="seg num" role="radiogroup" aria-label="Nozzle">
          {nozzles.map((n) => (
            <button key={n.name} type="button" role="radio" aria-checked={n.name === current?.name}
              onClick={() => choosePrinter(n.name)}>
              {n.d}
            </button>
          ))}
        </div>
        <span className="seg-unit">mm</span>
      </div>
    </div>
  );
}

const MOD = /Mac|iPhone|iPad/.test(navigator.platform) ? '⌘' : 'Ctrl+';

function DropZone() {
  return (
    <div className="fr-drop">
      <Icon name="design_import_mesh" size={40} className="fr-drop-icon" />
      <div className="fr-drop-title">Drop a model</div>
      <div className="fr-drop-types">STL · OBJ · 3MF</div>
      <div className="fr-actions">
        <button type="button" className="btn-primary fr-add" onClick={openFileDialog} title={`Add model (${MOD}I)`}>
          Add model<kbd>{MOD}I</kbd>
        </button>
        <button type="button" className="btn-ghost" onClick={openProjectDialog} title={`Open project (${MOD}O)`}>Open project</button>
      </div>
      <div className="fr-samples">
        <span>or try</span>
        {SAMPLES.map((s) => (
          <button key={s.file} type="button" className="fr-sample" title={s.tip} onClick={() => void addSample(s.url, s.file)}>
            {s.label}
          </button>
        ))}
      </div>
    </div>
  );
}

function afterFirstVisit() {
  const root = document.documentElement;
  const check = () => {
    const s = getState();
    if (s.job.state !== 'idle') { delete root.dataset.nudge; stop(); return; }
    if (s.objects.length) { localStorage.setItem(SETUP_KEY, '1'); root.dataset.nudge = 'slice'; } else delete root.dataset.nudge;
  };
  const stop = subscribeStore(check);
  check();
}

export default function EmptyPlate() {
  const [firstVisit] = useState(() => !localStorage.getItem(SETUP_KEY) && !localStorage.getItem('orca.printer'));
  useEffect(() => () => { if (firstVisit) afterFirstVisit(); }, []);
  return (
    <div className="empty-plate">
      {firstVisit && <PrinterChoice />}
      <DropZone />
    </div>
  );
}

export function SetupWizard({ close }: { close(): void }) {
  const count = useStore((s) => s.objects.length);
  const start = useRef(count);
  useEffect(() => { if (count > start.current) close(); }, [count]);
  return (
    <Modal label="Setup Wizard" onClose={close} className="small setup-wizard" returnFocus=".topbar .menu-title">
      <ModalTitle title="Setup Wizard" onClose={close} />
      <div className="setup-wizard-body">
        <PrinterChoice />
        <DropZone />
      </div>
      <div className="modal-buttons">
        <span style={{ flex: 1 }} />
        <button type="button" className="btn primary" data-autofocus onClick={close}>OK</button>
      </div>
    </Modal>
  );
}
