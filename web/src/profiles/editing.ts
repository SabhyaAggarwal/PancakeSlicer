import type { PresetType } from '../api/types';
import { createMachine,type Machine } from '../flow/fsm';
export type EditState='clean'|'modified';
type EditEvent={type:'EDITS';n:number};
interface EditContext {n:number}
const machines=new Map<string,Machine<EditState,EditEvent,EditContext>>();
export function editing(type:PresetType,slot=0) {
  const key=`${type}:${slot}`,existing=machines.get(key);if(existing)return existing;
  const on={EDITS:[{target:'modified' as const,guard:(_:EditContext,e:EditEvent)=>e.n>0,action:(_:EditContext,e:EditEvent)=>({n:e.n})},{target:'clean' as const,action:()=>({n:0})}]};
  const machine=createMachine<EditState,EditEvent,EditContext>({id:`preset:${key}`,initial:'clean',context:{n:0},states:{clean:{on},modified:{on}}});
  machines.set(key,machine);return machine;
}
