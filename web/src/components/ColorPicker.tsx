import { lazy, Suspense, type ComponentProps } from 'react';
const ColorControl = lazy(() => import('./ColorControl'));
export function ColorPicker(props: ComponentProps<typeof ColorControl>) {
  return <Suspense fallback={<span className="color-trigger" aria-busy="true" style={{backgroundColor: /^#[\da-f]{6}$/i.test(props.value) ? props.value : undefined}}/>}><ColorControl {...props}/></Suspense>;
}
