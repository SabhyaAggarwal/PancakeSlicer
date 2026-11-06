
export function disabledCommandReason(label: string): string {
  if (/undo/i.test(label)) return 'There is nothing to undo.';
  if (/redo/i.test(label)) return 'There is nothing to redo.';
  if (/paste/i.test(label)) return 'Copy an object or its settings first.';
  if (/export|send|print plate/i.test(label)) return 'Slice the plate before exporting or sending it.';
  if (/remove instance/i.test(label)) return 'The object must have more than one instance.';
  if (/save/i.test(label)) return 'There are no pending changes to save.';
  if (/delete|clone|cut|copy|split|object|paint|move|rotate|scale|measure/i.test(label)) return 'Select an editable object on the plate first.';
  return 'This action is unavailable for the current selection or printer.';
}
