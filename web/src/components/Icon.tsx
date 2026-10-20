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
}) {
  return (
    <button
      type="button"
      className={`icon-btn ${props.active ? 'active' : ''} ${props.className ?? ''}`}
      title={props.title}
      disabled={props.disabled}
      onClick={props.onClick}
    >
      <Icon name={props.icon} size={props.size ?? 16} />
    </button>
  );
}
