import { useEffect, useState } from 'react';
import { Modal, ModalBody, ModalFooter, ModalTitle } from './Modal';

export type BuildAbout = { version: string; revision: string; image: string; license: string; source_url: string; source_configured: boolean; source_sha256: string };

export function AboutContent({ about }: { about: BuildAbout }) {
  const link = (href: string, text: string) => <a href={href} target="_blank" rel="noreferrer">{text}</a>;
  return <dl className="modal-grid">
    <dt>Version</dt><dd data-truncated="true" data-full-text={about.revision}>{about.version}</dd>
    {about.image && <><dt>Image</dt><dd>{about.image}</dd></>}
    <dt>Licence</dt><dd>{link('https://www.gnu.org/licenses/agpl-3.0.html', about.license)}</dd>
    <dt>Source</dt><dd>{about.source_configured ? link(about.source_url, 'Source code of this version') : <span role="alert">Not configured (required by AGPL-3.0 §13)</span>}</dd>
    <dt>Licences</dt><dd>{link('/third-party-licences.txt', 'Third-party licences')}</dd>
  </dl>;
}

export default function About({ close }: { close: () => void }) {
  const [about, setAbout] = useState<BuildAbout | null>(null);
  const [error, setError] = useState('');
  const [attempt, setAttempt] = useState(0);
  useEffect(() => {
    const controller = new AbortController();
    setError('');
    fetch('/api/about', { signal: controller.signal }).then(async (response) => {
      if (!response.ok) throw new Error(`Server returned ${response.status}`);
      setAbout(await response.json() as BuildAbout);
    }).catch((e: unknown) => {
      if (!controller.signal.aborted) setError(e instanceof Error ? e.message : 'Service unavailable');
    });
    return () => controller.abort();
  }, [attempt]);
  return <Modal size="m" label="About Nonplanar OrcaSlicer" onClose={close}>
    <ModalTitle title="Nonplanar OrcaSlicer" onClose={close} />
    <ModalBody>
      {about ? <AboutContent about={about} /> : error ? <p role="alert">Version information did not load: {error}</p> : <p role="status">Loading…</p>}
    </ModalBody>
    <ModalFooter>
      {error && <button type="button" className="btn" onClick={() => setAttempt(attempt + 1)}>Retry</button>}
      <button type="button" className="btn-primary" onClick={close}>Close</button>
    </ModalFooter>
  </Modal>;
}
