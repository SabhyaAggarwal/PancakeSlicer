import { Checkbox } from 'radix-ui';
import type { ComponentProps } from 'react';
type Props = Omit<ComponentProps<typeof Checkbox.Root>, 'onCheckedChange'> & {onCheckedChange?(checked: boolean): void};
export function Check({className = '', onCheckedChange, ...props}: Props) {
  return <Checkbox.Root {...props} type="button" onCheckedChange={next => onCheckedChange?.(next === true)} className={`control-check ${className}`}><Checkbox.Indicator className="control-check-mark">{props.checked === 'indeterminate' ? '−' : '✓'}</Checkbox.Indicator></Checkbox.Root>;
}
