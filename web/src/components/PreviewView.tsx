import { useStore } from '../state/store';

export function PreviewView() {
  const job = useStore((s) => s.job);
  const summary = (job.status?.record as { text_summary?: Record<string, string> } | undefined)?.text_summary;
  const rows: [string, string | undefined][] = [
    ['Estimated time', summary?.estimated_printing_time ?? summary?.print_time],
    ['Used filament (g)', summary?.total_weight ?? summary?.filament_used_g],
    ['Used filament (m)', summary?.filament_used_m],
    ['Layers', summary?.layers ?? summary?.layer_count],
  ];
  return (
    <div className="preview-placeholder">
      <div className="preview-card">
        <h2>Preview</h2>
        {job.state === 'done' ? (
          <>
            <p>Plate 1 is sliced. The toolpath viewer (layers, moves, the Non-planar view) arrives with the move stream.</p>
            <table className="sliced-info">
              <tbody>
                {rows.filter(([, v]) => v).map(([k, v]) => (
                  <tr key={k}>
                    <td>{k}</td>
                    <td>{v}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </>
        ) : (
          <p>No sliced result. Slice the plate to see the result here.</p>
        )}
      </div>
      <div className="layer-slider-placeholder" title="Layer slider">
        <div className="ls-track" />
      </div>
    </div>
  );
}
