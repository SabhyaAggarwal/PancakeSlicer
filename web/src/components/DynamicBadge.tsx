export function DynamicBadge({ label = 'Dynamic' }: { label?: string }) {
  return <span className="dynamic-badge" role="img" aria-label={label}>✦</span>;
}
