import { getState } from '../state/store';
import { useSyncExternalStore } from 'react';
import { isDark, subscribeTheme } from '../theme';

export function Icon({ name, size = 16, className, title }: { name: string; size?: number; className?: string; title?: string }) {
  const dark = useSyncExternalStore(subscribeTheme, isDark);
  return <img src={`/images/${dark ? 'dark/' : ''}${name}.svg`} width={size} height={size} className={className} alt="" title={title} draggable={false} />;
}

export function IconButton(props: {
  icon: string;
  title: string;
  onClick?: React.MouseEventHandler<HTMLButtonElement>;
  disabled?: boolean;
  active?: boolean;
  size?: number;
  className?: string;
  disabledReason?: string;
}) {
  const state = getState();
  const reason = props.disabled ? props.disabledReason ?? (/undo/i.test(props.icon) ? 'There is nothing to undo.' : /redo/i.test(props.icon) ? 'There is nothing to redo.' : !state.objects.length ? 'Add an object to the plate first.' : !state.selected ? 'Select an object on the plate first.' : 'This action is unavailable for the current selection or printer.') : undefined;
  return (
    <button
      type="button"
      className={`icon-btn ${props.active ? 'active' : ''} ${props.className ?? ''}`}
      title={reason ? `${props.title}\n${reason}` : props.title}
      aria-label={props.title}
      aria-description={reason}
      disabled={props.disabled}
      onClick={props.onClick}
    >
      <Icon name={props.icon} size={props.size ?? 16} />
    </button>
  );
}
