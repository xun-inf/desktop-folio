import { BrowserWindow, screen, type WebContents } from 'electron';
import { randomUUID } from 'node:crypto';
import type {
  WindowConfig,
  ResolvedWindowOptions,
  WindowOptions,
  WindowControl,
  WindowState,
} from './contract.js';
type SenderContext = { sender: WebContents };

interface RecordEntry {
  id: string;
  window: BrowserWindow;
  pageUrl: string;
  ready: boolean;
  showRequested: boolean;
  businessId?: string;
  occupied: boolean;
  loadVersion: number;
  loadFailed: boolean;
}

export class PopupWindowMgr {
  #records = new Map<string, RecordEntry>();
  #stopped = false;
  #config: WindowConfig | null = null;
  #stateOutlet: (sender: WebContents, state: WindowState) => void = () => {};
  setStateOutlet(outlet: (sender: WebContents, state: WindowState) => void): void {
    this.#stateOutlet = outlet;
  }
  initialize(config: WindowConfig): void {
    if (this.#config !== null) throw new Error('Window manager already initialized');
    this.#config = { ...config, pageUrls: { ...config.pageUrls } };
  }
  register(id: string, wid: number, businessId?: string, pageUrl?: string): void {
    if (this.#stopped) throw new Error('Application is shutting down');
    if (typeof id !== 'string' || !id || id.length > 256 || this.#records.has(id))
      throw new Error('Invalid or duplicate window ID');
    const window = BrowserWindow.fromId(wid);
    if (!window || [...this.#records.values()].some((record) => record.window === window))
      throw new Error('Invalid or duplicate BrowserWindow');
    const expected =
      pageUrl ??
      (id === 'main'
        ? this.#config?.pageUrls.Main
        : id === 'child'
          ? this.#config?.pageUrls.Settings
          : undefined);
    if (!expected) throw new Error('Window page must be registered');
    const record: RecordEntry = {
      id,
      window,
      pageUrl: expected,
      ready: false,
      showRequested: false,
      businessId,
      occupied: false,
      loadVersion: 0,
      loadFailed: false,
    };
    this.#records.set(id, record);
    window.on('page-title-updated', (event) => event.preventDefault());
    window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
    window.webContents.on('did-start-navigation', (details) => {
      if (details.isMainFrame && !details.isSameDocument) record.ready = false;
    });
    window.webContents.on('render-process-gone', () => {
      record.ready = false;
    });
    const changed = () => {
      if (!window.isDestroyed() && record.ready)
        this.#stateOutlet(window.webContents, { maximized: window.isMaximized() });
    };
    window.on('maximize', changed);
    window.on('unmaximize', changed);
    if (id === 'child')
      window.on('close', (event) => {
        if (!this.#stopped) {
          event.preventDefault();
          this.hide(id);
        }
      });
    window.once('closed', () => this.#records.delete(id));
  }
  sender(event: SenderContext): RecordEntry | undefined {
    return [...this.#records.values()].find(
      (record) => !record.window.isDestroyed() && record.window.webContents === event.sender,
    );
  }
  requireSender(event: SenderContext): RecordEntry {
    const record = this.sender(event);
    if (!record || this.#stopped) throw new Error('Unknown page renderer');
    return record;
  }
  rendererReady(event: SenderContext): void {
    const record = this.requireSender(event);
    record.ready = true;
    if (record.showRequested) this.#show(record);
  }
  list(): Array<{ id: string; wid: number }> {
    return [...this.#records.values()].map(({ id, window }) => ({ id, wid: window.id }));
  }
  find(businessId: string): string | null {
    return (
      [...this.#records.values()].find(
        (record) => record.businessId === businessId && (record.id !== 'child' || record.occupied),
      )?.id ?? null
    );
  }
  claimChild(businessId: string): { reload: boolean } | null {
    if (this.#stopped) throw new Error('Application is shutting down');
    const record = this.#records.get('child');
    if (!record || record.occupied) return null;
    const reload =
      record.loadFailed || (record.businessId !== undefined && record.businessId !== businessId);
    record.businessId = businessId;
    record.occupied = true;
    return { reload };
  }
  load(id: string, url: string): Promise<void> {
    const record = this.#record(id);
    const version = ++record.loadVersion;
    record.loadFailed = false;
    record.ready = false;
    record.pageUrl = url;
    return record.window.loadURL(url).catch((error) => {
      if (this.#records.get(id) === record && record.loadVersion === version) {
        record.loadFailed = true;
        this.hide(id);
      }
      throw error;
    });
  }
  show(id: string): void {
    const record = this.#record(id);
    record.showRequested = true;
    if (record.window.webContents.isCrashed()) record.window.reload();
    else if (record.ready) this.#show(record);
  }
  hide(id: string): void {
    const record = this.#records.get(id);
    if (!record) throw new Error('Unknown window: ' + id);
    record.showRequested = false;
    record.occupied = false;
    record.window.hide();
    this.#records.get('main')?.window.focus();
  }
  control(event: SenderContext, action: unknown): { maximized: boolean } {
    const { window } = this.requireSender(event);
    switch (action as WindowControl) {
      case 'minimize':
        window.minimize();
        break;
      case 'maximize':
        window.maximize();
        break;
      case 'unmaximize':
        window.unmaximize();
        break;
      case 'toggleMaximize':
        window.isMaximized() ? window.unmaximize() : window.maximize();
        break;
      case 'close':
        window.close();
        break;
      default:
        throw new Error('Unknown window control');
    }
    return { maximized: !window.isDestroyed() && window.isMaximized() };
  }
  async showWindow(value: unknown): Promise<string> {
    const options = this.#options(value),
      config = this.#config!;
    const businessId = options.id ?? (options.reuse ? 'page:' + options.page : randomUUID());
    const existing = this.find(businessId);
    if (existing !== null) {
      this.show(existing);
      return existing;
    }
    const claim = options.reuse ? this.claimChild(businessId) : null;
    const id = claim ? 'child' : businessId;
    const url = new URL(config.pageUrls[options.page]);
    for (const [key, value] of Object.entries(options.queryParams ?? {}))
      url.searchParams.set(key, String(value));
    url.searchParams.set('id', id);
    if (claim) {
      const claimed = this.#record(id),
        child = claimed.window;
      let generation = claimed.loadVersion;
      try {
        this.#layout(child, options);
        if (claim.reload || child.webContents.getURL() !== url.href) {
          const loaded = this.load(id, url.href);
          generation = claimed.loadVersion;
          this.show(id);
          await loaded;
        } else this.show(id);
        return id;
      } catch (error) {
        if (this.#records.get(id) === claimed && claimed.loadVersion === generation) this.hide(id);
        throw error;
      }
    }
    const window = new BrowserWindow({
      parent: this.#record('main').window,
      frame: false,
      skipTaskbar: true,
      width: options.size.width,
      height: options.size.height,
      minWidth: options.size.minWidth ?? 320,
      minHeight: options.size.minHeight ?? 240,
      show: false,
      backgroundColor: '#0f141a',
      webPreferences: {
        preload: config.preload,
        nodeIntegration: false,
        contextIsolation: true,
        sandbox: true,
      },
    });
    window.setMenu(null);
    try {
      this.register(id, window.id, businessId, url.href);
      this.#layout(window, options);
      const loaded = this.load(id, url.href);
      this.show(id);
      await loaded;
      return id;
    } catch (error) {
      if (!window.isDestroyed()) window.destroy();
      throw error;
    }
  }
  closeWindow(value: unknown): void {
    if (typeof value !== 'string' || !value || value === 'main')
      throw new Error('Invalid business window ID');
    const id = this.find(value) ?? value;
    if (id === 'child') this.hide(id);
    else this.#records.get(id)?.window.close();
  }
  closeAllWindow(): void {
    for (const id of [...this.#records.keys()]) if (id !== 'main') this.closeWindow(id);
  }
  stop(): void {
    this.#stopped = true;
    for (const [id, record] of this.#records) {
      if (id !== 'main') record.window.hide();
    }
  }
  destroy(): void {
    for (const [id, record] of [...this.#records]) if (id !== 'main') record.window.destroy();
  }
  #record(id: string): RecordEntry {
    if (this.#stopped) throw new Error('Application is shutting down');
    const record = this.#records.get(id);
    if (!record || record.window.isDestroyed()) throw new Error('Window unavailable: ' + id);
    return record;
  }
  #show(record: RecordEntry): void {
    record.window.show();
    if (record.window.isMinimized()) record.window.restore();
    record.window.focus();
  }
  #options(value: unknown): ResolvedWindowOptions {
    if (
      !value ||
      typeof value !== 'object' ||
      !('page' in value) ||
      typeof value.page !== 'string' ||
      !['Settings', 'SecondScreen'].includes(value.page) ||
      !this.#config
    )
      throw new Error('Unknown window page');
    const options = JSON.parse(JSON.stringify(value)) as WindowOptions;
    if (options.reuse !== undefined && typeof options.reuse !== 'boolean')
      throw new Error('Invalid reuse option');
    if (options.center !== undefined && typeof options.center !== 'boolean')
      throw new Error('Invalid center option');
    if (
      options.title !== undefined &&
      (typeof options.title !== 'string' || options.title.length > 256)
    )
      throw new Error('Invalid title');
    if (
      options.id !== undefined &&
      (typeof options.id !== 'string' ||
        !options.id ||
        options.id.length > 256 ||
        ['main', 'child'].includes(options.id))
    )
      throw new Error('Invalid business window ID');
    if (
      options.queryParams !== undefined &&
      (!options.queryParams ||
        typeof options.queryParams !== 'object' ||
        Array.isArray(options.queryParams) ||
        Object.entries(options.queryParams).some(
          ([key, val]) =>
            !['streamId'].includes(key) ||
            !['string', 'number', 'boolean'].includes(typeof val) ||
            String(val).length > 64,
        ))
    )
      throw new Error('Invalid page query');
    options.size ??= { width: 800, height: 600 };
    if (!options.size || typeof options.size !== 'object') throw new Error('Invalid window size');
    for (const key of ['width', 'height', 'minWidth', 'minHeight'] as const) {
      const size = options.size[key];
      if (size !== undefined && (!Number.isInteger(size) || size <= 0 || size > 16384))
        throw new Error('Invalid window size');
    }
    if (!options.size.width || !options.size.height) throw new Error('Window size is incomplete');
    return { ...options, size: options.size };
  }
  #layout(window: BrowserWindow, options: ResolvedWindowOptions): void {
    const bounds = this.#record('main').window.getBounds(),
      display = screen.getDisplayMatching(bounds).workArea;
    const width = Math.min(options.size.width, display.width),
      height = Math.min(options.size.height, display.height);
    window.setMinimumSize(
      Math.min(options.size.minWidth ?? 320, width),
      Math.min(options.size.minHeight ?? 240, height),
    );
    const next: Partial<Electron.Rectangle> = { width, height };
    if (options.center !== false) {
      next.x = Math.max(
        display.x,
        Math.min(
          Math.round(bounds.x + (bounds.width - width) / 2),
          display.x + display.width - width,
        ),
      );
      next.y = Math.max(
        display.y,
        Math.min(
          Math.round(bounds.y + (bounds.height - height) / 2),
          display.y + display.height - height,
        ),
      );
    }
    window.setBounds(next);
    window.setTitle(options.title || options.page);
  }
}
export const popupWindowMgr = new PopupWindowMgr();
