export const VENDORS = ['BBL', 'Prusa', 'Custom'];
export const DEMO_MODELS = ['Bambu Lab A1 mini', 'Bambu Lab A1', 'Bambu Lab P1S', 'Prusa MINI', 'Prusa MINI IS', 'Prodcan3 5-axis BC table', 'Fairino FR3 cell'];
export const presetsUrl = (vendor: string, models: string[]) => `/api/presets?${new URLSearchParams([['vendor', vendor], ...models.map((m) => ['model', m])])}`;
