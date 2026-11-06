import { forwardRef,type ComponentPropsWithoutRef } from 'react';
import { RadioGroup } from 'radix-ui';

export const Radio=forwardRef<HTMLButtonElement,ComponentPropsWithoutRef<typeof RadioGroup.Item>>(function Radio({className='',...props},ref){
  return <RadioGroup.Item {...props} ref={ref} className={`orca-radio ${className}`}><RadioGroup.Indicator className="orca-radio-indicator"/></RadioGroup.Item>;
});
