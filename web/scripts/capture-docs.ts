





import { execFileSync } from 'node:child_process';
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { roofPair } from './roof-shots.ts';

const [base = 'http://127.0.0.1:27441/', out = 'docs/user-guide/img'] = process.argv.slice(2);
const { chromium } = await import(process.env.PLAYWRIGHT_CORE ?? 'playwright-core');
const browser = await chromium.launch({ executablePath: process.env.CHROME, args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const context = await browser.newContext({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 2, colorScheme: 'dark' });
const page = await context.newPage();
page.on('response',async r=>{if(r.status()>=400 && r.url().includes('/api/')) console.log(r.status(),r.url(),(await r.text().catch(()=>'<body unavailable>')).slice(0,1500));});
page.on('pageerror',e=>console.log('pageerror',e.message));
mkdirSync(out, { recursive: true });
const tmp = path.join(out, '.png');
mkdirSync(tmp, { recursive: true });

async function save(name: string, shot: (file: string) => Promise<unknown>) {
  const png = path.join(tmp, `${name}.png`);
  await shot(png);
  execFileSync('cwebp', ['-quiet', '-q', '80', png, '-o', path.join(out, `${name}.webp`)]);
}
const state = <T>(f: string) => page.evaluate(f) as Promise<T>;
const ready = () => page.waitForFunction(() => (window as any).__plater?.getState().profileLibraryLoad?.state === 'ready', null, { timeout: 60_000 });

await page.goto(base, { waitUntil: 'load' });
await ready();
const full = async (name: string, keepPointer = false) => { if (!keepPointer) await page.mouse.move(1430, 880); await page.evaluate(()=>(window as any).__plater.setState({toast:null,projectNotice:null})); await page.waitForTimeout(700); await page.evaluate(()=>(window as any).__plater.setState({toast:null,projectNotice:null})); await save(name, file => page.screenshot({path:file})); console.log('captured', name); };
if (!process.env.RESUME) {
await page.getByRole('dialog', {name:'Setup', exact:true}).waitFor();
await full('01-welcome');
await page.getByRole('button', {name:'Take the tour', exact:true}).click();
for(let i=1;i<=3;i++){ await full(`02-tour-${i}`); await page.getByRole('button',{name:'Next',exact:true}).click(); }
await page.getByRole('searchbox',{name:'Search printers'}).fill('A1 mini');
await page.locator('.setup-model').filter({hasText:'A1 mini'}).first().click();
await full('03-setup-printer');
await page.getByRole('button',{name:'Next',exact:true}).click();
await full('04-setup-profiles');
await page.getByRole('button',{name:'Next',exact:true}).click();
await full('05-setup-connection');
await page.getByRole('button',{name:'Skip',exact:true}).click();
await full('06-setup-filaments');
await page.getByRole('button',{name:'Done',exact:true}).click();
await page.getByRole('button',{name:'Start printing',exact:true}).waitFor({timeout:60000});
await full('07-setup-ready');
await page.getByRole('button',{name:'Start printing',exact:true}).click();
}

await page.getByRole('button', { name: 'Projects', exact: true }).first().click();
await page.getByRole('button', { name: 'New project', exact: true }).first().click();
await page.waitForFunction(() => (window as any).__plater.getState().tab === 'prepare');
await page.evaluate(async () => {
 const p=(window as any).__plater,s=p.getState();
 const {createProfileFromBase}=await import('/src/profiles/catalog.ts');
 const {catalogKey}=await import('/src/profiles/model.ts');
 const base=s.filaments.find((u:any)=>/Generic PLA.*A1M/.test(u.name));
 if(!base)throw new Error('No native A1 mini PLA: '+s.filaments.map((f:any)=>f.name).join(','));
 const key=await createProfileFromBase('filament',catalogKey(base),'Docs PLA');
 p.setState({filamentSlots:[key]});
});
await page.getByRole('button', { name: 'Benchy', exact: true }).click();
await page.waitForFunction(() => (window as any).__plater.getState().objects.length === 1 && !(window as any).__plater.getState().loading, null, { timeout: 60_000 });
const facts: Record<string, unknown> = {};
await full('08-benchy-prepare');
for (const [name, mode] of [['benchy-flat', 'off'], ['benchy-curved', 'curved_tops']] as const) {
  await page.evaluate((m) => { const p = (window as any).__plater; p.setState((s: any) => ({ tab: 'prepare', dirty: { ...s.dirty, process: { ...s.dirty.process, nonplanar_mode: m } } })); }, mode);
  const before = await state<string | null>('window.__plater.getState().job.id ?? null');
  await page.getByRole('button', { name: /^Slice/ }).first().click();
  await page.waitForFunction((id) => { const j = (window as any).__plater.getState().job; return j.id !== id && ['done', 'failed'].includes(j.state); }, before, { timeout: 300_000, polling: 500 });
  if (await state<string>('window.__plater.getState().job.state') !== 'done') throw new Error(`${name}: slice failed: ${await state('window.__plater.getState().job.error')}`);
  await page.evaluate(() => (window as any).__plater.setState({ tab: 'preview' }));
  await page.waitForFunction(() => (window as any).__preview?.data?.done, null, { timeout: 120_000, polling: 300 });
  await page.evaluate(() => window.dispatchEvent(new CustomEvent('camera-fit')));
  await page.mouse.move(1430, 880);
  await page.waitForTimeout(1500);
  facts[name] = await page.evaluate(() => ({ time: document.querySelector('.lg-stat-v')?.textContent, curved: document.querySelector('.np-summary-line')?.textContent ?? null }));
  await full(name+'-preview');
  const canvas = page.locator('.preview-canvas canvas').first();
  const box = (await canvas.boundingBox())!;

  const side = Math.min(box.width, box.height) * 0.62;
  await save(name, (file) => page.screenshot({ path: file, clip: { x: box.x + (box.width - side) / 2 + 60, y: box.y + (box.height - side) / 2, width: side, height: side } }));
}

writeFileSync(path.join(out,'measurements.json'),JSON.stringify(facts,null,2)+'\n');
await page.getByRole('button',{name:'G-code viewer (C)',exact:true}).click();
await page.locator('.gcode-window').waitFor();
await full('10-gcode-viewer');
await page.getByRole('button',{name:'Close G-code',exact:true}).click();
const downloaded=page.waitForEvent('download',{timeout:30000});
await page.getByRole('button',{name:'Export sliced file',exact:true}).click();
const download=await downloaded;
facts.export={filename:download.suggestedFilename()};
await full('09-export');
await page.evaluate(()=>(window as any).__plater.setState({tab:'prepare'}));
for (const tab of ['Quality','Strength','Support','Multimaterial','Others']) {
 await page.getByRole('tab',{name:tab,exact:true}).first().click();
 while(await page.locator('.sidebar .optgroup-head[aria-expanded=false]').count()) await page.locator('.sidebar .optgroup-head[aria-expanded=false]').first().click();
 await full('11-settings-'+tab.toLowerCase());
}
await page.getByRole('tab',{name:'Quality',exact:true}).first().click();

await page.evaluate(() => (window as any).__plater.setState({ tab: 'prepare', presetUx: { kind: 'presets', type: 'process', name: (window as any).__plater.getState().process } }));
await full('12-profiles-list');
await page.getByRole('tab', { name: '✦ Dynamic', exact: true }).first().click();
await page.locator('.dyn-tab').first().waitFor();
await page.waitForTimeout(600);
await save('dynamic', (file) => page.locator('.dyn-tab').first().screenshot({ path: file }));
await full('13-dynamic-formulas');
await page.locator('.dyn-table').scrollIntoViewIfNeeded();
await full('14-dynamic-combinations');
await page.getByRole('button',{name:'Close profiles',exact:true}).first().click();


await page.evaluate(()=>{const p=(window as any).__plater;p.setState({presetUx:{kind:'presets',type:'process',name:p.getState().process}});});
for (const tab of ['Quality','Strength','Speed','Support']) {
 await page.getByRole('tab',{name:tab,exact:true}).first().click();
 await full('settings-editor-'+tab.toLowerCase());
}
await page.getByRole('tab',{name:'Quality',exact:true}).first().click();
const glyph = page.locator('.pf-full-editor .source-glyph').first();
await glyph.scrollIntoViewIfNeeded();
await glyph.hover();
await page.waitForTimeout(700);
const row = (await glyph.evaluateHandle((el) => el.closest('.opt-line') ?? el.parentElement!)).asElement()!;
const r = (await row.boundingBox())!;
await save('printer-values', (file) => page.screenshot({ path: file, clip: { x: Math.max(0, r.x - 8), y: Math.max(0, r.y - 8), width: Math.min(420, r.width + 16), height: r.height + 64 } }));

await full('15-computed-tooltip', true);
await page.getByRole('button',{name:'Close profiles',exact:true}).first().click();
await page.evaluate(()=>{const p=(window as any).__plater,s=p.getState();p.setState({presetUx:{kind:'presets',type:'machine',name:s.printer,edit:true}});});
await page.getByRole('tab',{name:'Print overrides',exact:true}).click();
await full('16-printer-overrides');
await page.getByRole('button',{name:'Close profiles',exact:true}).first().click();
await page.evaluate(()=>{const p=(window as any).__plater,s=p.getState();p.setState({dirty:{...s.dirty,process:{...s.dirty.process,line_width:'0.47'}}});});
await page.locator('.pf-changes-trigger').first().click();
await full('17-save-as-new');
await page.keyboard.press('Escape');
await page.evaluate(()=>{const p=(window as any).__plater,s=p.getState();p.setState({dirty:{...s.dirty,process:{nonplanar_mode:'curved_tops'}},tab:'preview'});});
await page.evaluate(()=> (window as any).__plater.setState({appDialog:'send'}));
await full('18-send');
await page.keyboard.press('Escape');
await page.getByRole('button',{name:'Projects',exact:true}).first().click();
await full('19-projects');

const projectId=await page.evaluate(async()=>{const rows=await (await fetch('/api/projects')).json();return rows[0]?.id;});
if(!projectId)throw new Error('No saved project for lease capture');
await page.evaluate(async id=>{await (await import('/src/projects/api.ts')).openProject(id);},projectId);
const second=await browser.newContext({viewport:{width:1440,height:900},deviceScaleFactor:2,colorScheme:'dark'});
const other=await second.newPage();await other.goto(base);await other.waitForFunction(()=>(window as any).__plater?.getState().profileLibraryLoad.state==='ready');
await other.evaluate(async id=>{await (await import('/src/projects/api.ts')).openProject(id);},projectId);
await other.locator('.lease-bar').waitFor();
await save('20-take-control',file=>other.screenshot({path:file}));
await second.close();
for(const [name,tab] of [['prepare','prepare'],['preview','preview'],['projects','projects']] as const){
 await page.setViewportSize({width:390,height:844});
 await page.evaluate(t=>(window as any).__plater.setState({tab:t}),tab);
 await full('mobile-'+name);
}
await page.evaluate(()=>(window as any).__plater.setState({tab:'prepare',presetUx:{kind:'presets',type:'process',name:(window as any).__plater.getState().process}}));
await full('mobile-profiles');
await page.setViewportSize({width:1440,height:900});
await page.evaluate(()=>(window as any).__plater.setState({presetUx:null,tab:'prepare'}));

await page.evaluate(async()=>{ await (await import('/src/projects/api.ts')).newProject(); });
await page.getByRole('button',{name:'Benchy',exact:true}).click();
await page.waitForFunction(()=>(window as any).__plater.getState().objects.length===1 && !(window as any).__plater.getState().loading);
for (const [k, v] of Object.entries(await roofPair(page, 'roof', save))) facts[`roof-${k}`] = v;

writeFileSync(path.join(out, 'measurements.json'), JSON.stringify(facts,null,2)+'\n');
rmSync(tmp,{recursive:true,force:true});
await browser.close();
