import { useEffect, useState } from 'react';
import { slice } from '../actions';
import { noticeKind, noticeMs, sliceFailure } from '../notice-text';
import { setState, useStore } from '../state/store';
import { Icon } from './Icon';
import { InTopLayer } from './Modal';
import './notices.css';


export function Toast() {
  const toast = useStore((s) => s.toast);
  const seq = useStore((s) => s.toastSeq);
  const projectNotice = useStore((s) => s.projectNotice);
  const action = useStore((s) => (s.toast && s.toastAction?.msg === s.toast ? s.toastAction : null));
  const ms = useStore((s) => s.toastMs);
  const msg = toast ?? projectNotice;
  const [held, setHeld] = useState(false);
  useEffect(() => {
    if (!toast || held) return;
    const t = setTimeout(() => setState((s) => (s.toast === toast ? { toast: null } : {})), ms ?? noticeMs(toast));
    return () => clearTimeout(t);
  }, [toast, seq, held, ms]);
  useEffect(() => setHeld(false), [msg]);
  if (!msg) return null;
  const kind = toast ? noticeKind(msg) : 'info';
  const reload = /^Presets failed to load/i.test(msg);
  return (
    <InTopLayer>
      <div className={`toast toast-${kind}`} role={kind === 'error' ? 'alert' : 'status'}
        onMouseEnter={() => setHeld(true)} onMouseLeave={() => setHeld(false)}
        onFocus={() => setHeld(true)} onBlur={() => setHeld(false)}>
        <div className="toast-text">
          <div>{msg}</div>
        </div>
        {reload && !action && <button type="button" className="btn-ghost toast-action" onClick={() => location.reload()}>Reload</button>}
        {action && <button type="button" className="btn-ghost toast-action" onClick={() => { setState({ toast: null }); action.run(); }}>{action.label}</button>}
        {toast && (
          <button type="button" className="toast-close icon-btn" title="Close" aria-label="Close" onClick={() => setState({ toast: null })}>
            <Icon name="notification_close" size={14} />
          </button>
        )}
      </div>
    </InTopLayer>
  );
}

export function SliceFailed({ error, onClose }: { error: string; onClose(): void }) {
  const { title, step } = sliceFailure(error);
  const canRetry = useStore((s) => s.objects.length > 0);
  const [copied, setCopied] = useState(false);
  return (
    <div className="notification failed slice-failed" role="alert">
      <button className="notif-close icon-btn" title="Close" aria-label="Close" onClick={onClose}>
        <Icon name="notification_close" size={14} />
      </button>
      <div className="notif-title"><Icon name="error" size={16} /> {title}</div>
      <div className="notif-body">{step}</div>
      <div className="slice-failed-actions">
        {canRetry && <button type="button" className="btn-primary" onClick={() => void slice()}>Slice again</button>}
        <details>
          <summary>Details</summary>
          <pre className="slice-failed-raw">{error}</pre>
          <button type="button" className="notif-link" onClick={() => void navigator.clipboard?.writeText(error).then(() => setCopied(true))}>
            {copied ? 'Copied' : 'Copy'}
          </button>
        </details>
      </div>
    </div>
  );
}

const LOADING_WORDS: Record<string, string> = { Connecting: 'Starting', 'Loading presets': 'Loading printers' };

export function LoadingState({ text }: { text: string }) {
  const m = /^(.*?)\s+(\d+)%$/.exec(text);
  const label = LOADING_WORDS[text] ?? (m ? m[1] : text);
  const pct = m ? Number(m[2]) : null;
  return (
    <div className="loading-state" role="status" aria-live="polite">
      <div className="loading-label"><span>{label}</span>{pct !== null && <span>{pct}%</span>}</div>
      <div className={`loading-bar ${pct === null ? 'indeterminate' : ''}`} role="progressbar" aria-label={label}
        aria-valuemin={0} aria-valuemax={100} aria-valuenow={pct ?? undefined}>
        <div style={pct === null ? undefined : { width: `${pct}%` }} />
      </div>
    </div>
  );
}
