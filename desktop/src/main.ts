import { app, BrowserWindow, dialog, ipcMain, Menu, session, shell, type MenuItemConstructorOptions } from 'electron';
import { spawn, type ChildProcess } from 'node:child_process';
import { createHash, randomBytes } from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { ProjectStore } from './projects';
import { LegacyWorkspaceSource, type LegacyRead } from './legacy-workspace';
import { DESKTOP_ORIGIN, LegacyRendererSource, type LegacyRendererPartRead } from './legacy-renderer';

const MAC = process.platform === 'darwin';
const EXE = process.platform === 'win32' ? '.exe' : '';

let server: ChildProcess | null = null;
let origin = '';
let win: BrowserWindow | null = null;
let quitting = false;
let quitReady = false;
let closing = false;
let projects: ProjectStore | null = null;

app.commandLine.appendSwitch('enable-unsafe-swiftshader');

app.setPath('userData', process.env.ORCA_DESKTOP_USER_DATA ?? path.join(app.getPath('appData'), 'OrcaSlicer Web'));

const originalUserData = app.getPath('userData');
const originalSessionData = app.getPath('sessionData');
const legacyRenderer = new LegacyRendererSource(originalUserData, originalSessionData, () =>
  process.versions.electron === '45.2.1' && (process.platform === 'linux' || process.platform === 'darwin')
  && app.hasSingleInstanceLock() && app.getPath('userData') === originalUserData
  && originalSessionData === originalUserData
  && !['user-data-dir', 'profile-directory', 'session-data-dir'].some((name) => app.commandLine.hasSwitch(name)));
const sessionCacheRoot = path.join(app.getPath('appData'), 'OrcaSlicer Web Cache');
let ownedSessionDirectory: { path: string; dev: number; ino: number } | null = null;

function cleanSessionDirectory() {
  const owned = ownedSessionDirectory;
  if (!owned) return;
  ownedSessionDirectory = null;
  try {
    const stat = fs.lstatSync(owned.path);
    if (!stat.isDirectory() || stat.isSymbolicLink() || stat.dev !== owned.dev || stat.ino !== owned.ino) return;
    fs.rmSync(owned.path, { recursive: true, force: true });
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'ENOENT') console.error('Disposable Chromium cache cleanup did not finish.');
  }
}

try {
  fs.mkdirSync(sessionCacheRoot, { recursive: true, mode: 0o700 });
  const directory = fs.mkdtempSync(path.join(sessionCacheRoot, 'session-'));
  const stat = fs.lstatSync(directory);
  if ([originalUserData, originalSessionData].some((old) => path.resolve(old) === directory))
    throw new Error('Session isolation cannot use an original storage root.');
  ownedSessionDirectory = { path: directory, dev: stat.dev, ino: stat.ino };
  app.setPath('sessionData', directory);
} catch {
  cleanSessionDirectory();
  throw new Error('Chromium session-data isolation failed; no renderer was started.');
}
process.once('exit', cleanSessionDirectory);

if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  app.on('second-instance', () => {
    if (!win) return;
    if (win.isMinimized()) win.restore();
    win.focus();
  });
  app.whenReady().then(start).catch((e) => fail(String(e?.stack ?? e)));
}

function payload() {
  if (app.isPackaged) return { web: path.join(process.resourcesPath, 'web'), engine: path.join(process.resourcesPath, 'engine') };
  const build = path.join(__dirname, '..', 'build');
  const os = MAC ? 'mac' : process.platform === 'win32' ? 'win' : 'linux';
  return { web: path.join(build, 'web'), engine: process.env.ORCA_DESKTOP_ENGINE ?? path.join(build, `engine-${os}-${process.arch}`) };
}

interface BundleIdentity {
  client_source: string;
  server_source: string;
  native_source: string;
  native_binary_sha256: string;
  native_resources_sha256: string;
  server_binary_sha256: string;
  native_manifest_sha256: string;
  native_qualification_sha256: string;
}

async function fileHash(file: string) {
  const hash = createHash('sha256');
  for await (const chunk of fs.createReadStream(file)) hash.update(chunk);
  return hash.digest('hex');
}

async function bundleIdentity(web: string, engine: string): Promise<BundleIdentity> {
  const expected = JSON.parse(fs.readFileSync(path.join(engine, 'COMPONENTS.json'), 'utf8')) as BundleIdentity;
  const webIdentity = JSON.parse(fs.readFileSync(path.join(web, 'COMPONENTS.json'), 'utf8')) as BundleIdentity;
  for (const key of ['client_source', 'server_source', 'native_source'] as const) {
    if (!/^[a-f0-9]{40}$/.test(expected[key]) || expected[key] !== webIdentity[key]) throw new Error('Bundled source identity is incomplete or inconsistent.');
  }
  if (fs.readFileSync(path.join(web, 'REVISION.txt'), 'utf8').trim() !== expected.client_source) throw new Error('Bundled web revision differs from its component stamp.');
  for (const [name, key] of [
    [`orca-webapp-server${EXE}`, 'server_binary_sha256'], [`nonplanar_slice_tool${EXE}`, 'native_binary_sha256'],
    ['surface-manifest.json', 'native_manifest_sha256'], ['artifact-qualification.ready', 'native_qualification_sha256'],
  ] as const) {
    if (!/^[a-f0-9]{64}$/.test(expected[key]) || await fileHash(path.join(engine, name)) !== expected[key]) throw new Error('A bundled payload differs from its component stamp.');
  }
  if (!/^[a-f0-9]{64}$/.test(expected.native_resources_sha256)) throw new Error('Bundled resource identity is missing.');
  return expected;
}

function healthy(nonce: string, expected: BundleIdentity): Promise<boolean> {
  return new Promise((resolve) => {
    const req = http.get(`${DESKTOP_ORIGIN}/api/healthz`, { timeout: 1000, headers: { 'Cache-Control': 'no-store' } }, (res) => {
      if (res.statusCode !== 200) { res.resume(); resolve(false); return; }
      let body = '';
      res.setEncoding('utf8');
      res.on('data', (chunk: string) => {
        body += chunk;
        if (body.length > 65536) { req.destroy(); resolve(false); }
      });
      res.on('error', () => resolve(false));
      res.on('end', () => {
        try {
          const health = JSON.parse(body);
          const artifact = health.artifact_manifest;
          resolve(health.desktop_ready_nonce === nonce && health.server_revision === expected.server_source
            && ['profile_library_schema', 'profile_recovery_schema', 'workspace_schema'].every((key) => health[key] === 1)
            && artifact?.source_sha === expected.native_source && artifact.binary_sha256 === expected.native_binary_sha256
            && artifact.resources_sha256 === expected.native_resources_sha256);
        } catch { resolve(false); }
      });
    });
    req.on('error', () => resolve(false));
    req.on('timeout', () => { req.destroy(); resolve(false); });
  });
}

async function startServer(): Promise<string> {
  const { web, engine } = payload();
  const expected = await bundleIdentity(web, engine);
  const data = app.getPath('userData');
  const logs = path.join(data, 'logs');
  fs.mkdirSync(logs, { recursive: true });
  const log = fs.openSync(path.join(logs, 'server.log'), 'w');
  const lib = path.join(engine, 'lib');
  const surfaceManifest = path.join(engine, 'surface-manifest.json');
  const qualification = path.join(engine, 'artifact-qualification.ready');
  const env = { ...process.env };
  const nonce = randomBytes(32).toString('hex');
  env.ORCA_DESKTOP_READY_NONCE = nonce;
  delete env.ORCA_BUILD_REVISION;
  delete env.ORCA_BUILD_IMAGE;
  env.ORCA_WEB_FALLBACK_BACKEND = '';
  delete env.ORCA_WEB_SURFACE_MANIFEST;
  delete env.ORCA_WEB_ARTIFACT_MANIFEST;
  delete env.ORCA_WEB_ARTIFACT_QUALIFICATION;
  if (fs.existsSync(surfaceManifest)) env.ORCA_WEB_SURFACE_MANIFEST = surfaceManifest;
  if (fs.existsSync(qualification)) env.ORCA_WEB_ARTIFACT_QUALIFICATION = qualification;
  if (process.platform === 'linux' && fs.existsSync(lib)) env.LD_LIBRARY_PATH = [lib, env.LD_LIBRARY_PATH].filter(Boolean).join(':');
  let lastError = 'The bundled service did not prove readiness at the fixed desktop origin.';
  const child = spawn(path.join(engine, `orca-webapp-server${EXE}`), [
    '--listen', '127.0.0.1:29731',
    '--static', web,
    '--tool', path.join(engine, `nonplanar_slice_tool${EXE}`),
    '--resources', path.join(engine, 'resources'),
    '--data-dir', path.join(data, 'data'),
    '--job-root', path.join(data, 'jobs'),
    '--printer-network', 'lan',
  ], { env, stdio: ['ignore', log, log], windowsHide: true });
  fs.closeSync(log);
  server = child;
  let exited = false, ready = false;
  child.on('exit', (code, signal) => {
    exited = true;
    lastError = `Bundled service exited (${signal ?? code}); the fixed desktop origin may be occupied.`;
    if (ready && server === child && !quitting) fail('The bundled slicing service stopped.');
  });
  child.on('error', () => { exited = true; lastError = 'The bundled slicing service could not be started.'; });
  const deadline = Date.now() + 30000;
  while (!exited && Date.now() < deadline) {
    if (await healthy(nonce, expected) && !exited) { ready = true; return DESKTOP_ORIGIN; }
    await new Promise((r) => setTimeout(r, 100));
  }
  stopServer();
  throw new Error(lastError);
}

function stopServer() {
  const child = server;
  server = null;
  if (!child || child.exitCode !== null) return;
  child.kill('SIGTERM');
  setTimeout(() => { if (child.exitCode === null) child.kill('SIGKILL'); }, 3000).unref();
}

function fail(message: string) {
  if (quitting) return;
  quitting = true;
  dialog.showErrorBox('OrcaSlicer', message);
  stopServer();
  app.exit(1);
}

function send(command: string) {
  if (command === 'import' || command === 'export-gcode' || command === 'export-3mf') {
    void win?.webContents.executeJavaScript(
      `window.dispatchEvent(new CustomEvent('orca-desktop-menu', { detail: ${JSON.stringify(command)} }))`, true,
    ).catch(console.error);
    return;
  }
  win?.webContents.send('orca-desktop:menu', command);
}

function menu() {
  const item = (label: string, command: string, accelerator: string): MenuItemConstructorOptions =>
    ({ label, accelerator, click: () => send(command) });
  const template: MenuItemConstructorOptions[] = [
    ...(MAC ? [{ role: 'appMenu' } as MenuItemConstructorOptions] : []),
    {
      label: 'File',
      submenu: [
        item('New project', 'new', 'CmdOrCtrl+N'),
        item('Projects', 'projects', 'CmdOrCtrl+O'),
        item('Import', 'import', 'CmdOrCtrl+I'),
        { type: 'separator' },
        item('Export G-code', 'export-gcode', 'CmdOrCtrl+E'),
        item('Export project (.3mf)', 'export-3mf', 'CmdOrCtrl+Shift+E'),
        { type: 'separator' },
        MAC ? { role: 'close' } : { role: 'quit' },
      ],
    },
    {
      label: 'Edit',
      submenu: [
        item('Undo', 'undo', 'CmdOrCtrl+Z'),
        item('Redo', 'redo', MAC ? 'Shift+Cmd+Z' : 'Ctrl+Y'),
        { type: 'separator' },
        { role: 'cut' }, { role: 'copy' }, { role: 'paste' }, { role: 'selectAll' },
      ],
    },
    {
      label: 'View',
      submenu: [
        { role: 'resetZoom' }, { role: 'zoomIn' }, { role: 'zoomOut' }, { type: 'separator' },
        { role: 'togglefullscreen' }, { role: 'toggleDevTools' },
      ],
    },
    { role: 'windowMenu' },
  ];
  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
}

function storage() {
  const store = new ProjectStore(app.getPath('userData'));
  const legacy = new LegacyWorkspaceSource(app.getPath('userData'));
  projects = store;
  let legacyReady: Promise<void> | null = null;
  let exporting = false;
  const pendingWrites = new Set<Promise<unknown>>();
  const readers = new Set(['list', 'load', 'getBlob', 'hasBlob']);
  const ops = ['list', 'load', 'save', 'rename', 'duplicate', 'remove', 'restore', 'putBlob', 'getBlob', 'hasBlob', 'emptyTrash'] as const;
  for (const op of ops) {
    ipcMain.handle(`orca-desktop:projects:${op}`, async (e, ...args: unknown[]) => {
      if (!origin || new URL(e.senderFrame?.url ?? 'about:blank').origin !== origin) throw new Error('not the app');
      if (exporting && !readers.has(op)) throw new Error('Legacy desktop writes are disabled during workspace adoption. Retry service adoption; source files are retained.');
      const operation = (legacyReady ??= store.init()).then(() => (store[op] as (...a: unknown[]) => Promise<unknown>).apply(store, args));
      if (!readers.has(op)) pendingWrites.add(operation);
      try { return await operation; }
      catch (error) {
        if (error instanceof Error && ['ProjectConflictError', 'ProjectDeletedError'].includes(error.name))
          return { __orcaProjectError: { name: error.name, message: error.message } };
        throw error;
      } finally { pendingWrites.delete(operation); }
    });
  }
  ipcMain.handle('orca-desktop:legacy-projects:inventory', async (e) => {
    if (!origin || new URL(e.senderFrame?.url ?? 'about:blank').origin !== origin) throw new Error('not the app');
    exporting = true;
    await Promise.all([...pendingWrites]);
    await store.flush();
    return legacy.inventory();
  });
  ipcMain.handle('orca-desktop:legacy-projects:read-file', (e, request: LegacyRead) => {
    if (!origin || new URL(e.senderFrame?.url ?? 'about:blank').origin !== origin) throw new Error('not the app');
    return legacy.readFile(request);
  });
  ipcMain.handle('orca-desktop:legacy-renderer:inventory', (e) => {
    if (!origin || new URL(e.senderFrame?.url ?? 'about:blank').origin !== origin) throw new Error('not the app');
    return legacyRenderer.inventory();
  });
  ipcMain.handle('orca-desktop:legacy-renderer:read-part', (e, request: LegacyRendererPartRead) => {
    if (!origin || new URL(e.senderFrame?.url ?? 'about:blank').origin !== origin) throw new Error('not the app');
    return legacyRenderer.readPart(request);
  });
  ipcMain.handle('orca-desktop:bambu-connect', (e, jobId: unknown, filename: unknown) => {
    if (!origin || new URL(e.senderFrame?.url ?? 'about:blank').origin !== origin) throw new Error('not the app');
    return openInBambuConnect(jobId, filename);
  });
}

async function openInBambuConnect(jobId: unknown, filename: unknown) {
  if (typeof jobId !== 'string' || !/^[0-9a-f]{16,64}$/.test(jobId)) throw new Error('Invalid job.');
  if (typeof filename !== 'string' || !/\.gcode\.3mf$/i.test(filename) || /[\\/\u0000-\u001f]/.test(filename) || filename.length > 240) throw new Error('Invalid file name.');
  const dir = path.join(app.getPath('temp'), 'orca-bambu-connect');
  fs.mkdirSync(dir, { recursive: true });
  const target = path.join(dir, filename);
  await new Promise<void>((resolve, reject) => {
    http.get(`${DESKTOP_ORIGIN}/api/jobs/${jobId}/download`, (res) => {
      if (res.statusCode !== 200) { res.resume(); reject(new Error(`The sliced file is unavailable (HTTP ${res.statusCode}). Slice again.`)); return; }
      const out = fs.createWriteStream(target);
      res.pipe(out);
      out.on('finish', () => resolve());
      out.on('error', reject);
    }).on('error', reject);
  });
  const name = filename.replace(/\.gcode\.3mf$/i, '');
  const url = `bambu-connect://import-file?path=${encodeURIComponent(process.platform === 'win32' ? target.replace(/\\/g, '/') : target)}&name=${encodeURIComponent(name)}&version=1.0.0`;
  try { await shell.openExternal(url); }
  catch { throw new Error('Bambu Connect is not installed on this computer. Install it from bambulab.com, then send again.'); }
}

async function start() {
  menu();
  const [url] = await Promise.all([startServer(), storage()]);
  origin = url;
  const rendererSession = session.fromPartition('orca-service-renderer', { cache: false });
  if (rendererSession.isPersistent() || rendererSession.getStoragePath() !== null) throw new Error('The renderer requires an isolated in-memory session.');
  win = new BrowserWindow({
    width: 1440,
    height: 900,
    minWidth: 800,
    minHeight: 600,
    show: false,
    title: 'OrcaSlicer',
    backgroundColor: '#ffffff',
    webPreferences: { session: rendererSession, preload: path.join(__dirname, 'preload.js'), contextIsolation: true, sandbox: true, nodeIntegration: false, spellcheck: false },
  });
  win.once('ready-to-show', () => win?.show());
  win.on('close', (e) => {
    if (quitReady) return;
    e.preventDefault();
    void prepareQuit();
  });
  win.on('closed', () => { win = null; });
  const contents = win.webContents;
  contents.on('console-message', (details) => {
    if (details.level === 'warning' || details.level === 'error')
      console.error(`Renderer ${details.level}: ${details.message} (${details.sourceId}:${details.lineNumber})`);
  });
  contents.on('will-prevent-unload', (e) => e.preventDefault());
  contents.on('will-navigate', (e, target) => {
    if (new URL(target).origin !== origin) { e.preventDefault(); shell.openExternal(target); }
  });
  contents.setWindowOpenHandler(({ url: target }) => {
    if (/^https?:/.test(target) && new URL(target).origin !== origin) shell.openExternal(target);
    return { action: 'deny' };
  });
  await win.loadURL(url);
}

app.on('window-all-closed', () => app.quit());
app.on('before-quit', (e) => {
  if (quitReady) { quitting = true; return; }
  e.preventDefault();
  void prepareQuit();
});
app.on('will-quit', stopServer);
process.on('exit', stopServer);
process.on('SIGTERM', () => app.quit());
process.on('SIGINT', () => app.quit());

async function prepareQuit() {
  if (closing) return;
  closing = true;
  quitting = true;
  win?.setEnabled(false);
  let timer: ReturnType<typeof setTimeout> | undefined;
  try {
    const flush = async () => {
      if (win && !win.webContents.isDestroyed())
        await win.webContents.executeJavaScript('window.orcaDesktop?.flushBeforeClose?.()');
      await projects?.flush();
    };
    const finished = await Promise.race([
      flush().then(() => true),
      new Promise<false>((resolve) => { timer = setTimeout(() => resolve(false), 10000); }),
    ]);
    if (!finished) throw new Error('Saving is still in progress or has not been confirmed. The window remains open. Try closing again after saving finishes.');
    quitReady = true;
    app.quit();
  } catch (e) {
    quitting = false;
    closing = false;
    win?.setEnabled(true);
    dialog.showErrorBox('Close not confirmed', String(e));
  } finally {
    if (timer) clearTimeout(timer);
  }
}
