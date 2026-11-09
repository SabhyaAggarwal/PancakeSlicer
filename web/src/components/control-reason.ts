export function disabledCommandReason(label: string): string {
  if (/undo/i.test(label)) return 'Nothing left to undo.';
  if (/redo/i.test(label)) return 'Nothing left to redo.';
  if (/paste/i.test(label)) return 'Copy an object or its settings first before pasting.';
  if (/export|send|print plate/i.test(label)) return 'Slice the plate first before exporting or sending to your printer.';
  if (/remove instance/i.test(label)) return 'This object only has one copy on the plate.';
  if (/save/i.test(label)) return 'All caught up—no unsaved changes!';
  if (/delete|clone|cut|copy|split|object|paint|move|rotate|scale|measure/i.test(label)) return 'Select an object on the plate first.';
  return 'That action isn\'t available for your current selection or printer.';
}
