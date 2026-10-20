import { BaseWindow, BrowserWindow, WebContentsView, type WebContents } from 'electron';
import type { EventEmitter } from 'node:events';
import type { BrowserViewRect, BrowserViewEvent } from './contract.js';
import type { NativeWindowFacade } from '../mediaService/nativeWindowService.js';
interface BrowserView {
  id: string;
  owner: BrowserWindow;
  view: WebContentsView;
  window: BaseWindow;
  rect: BrowserViewRect | null;
  ready: boolean;
  cleanup(): void;
}
export class BrowserViewMgr {
  private readonly records = new Map<string, BrowserView>();
  constructor(
    private readonly native: NativeWindowFacade,
    private readonly emit: (sender: WebContents, event: BrowserViewEvent) => void,
  ) {}
  private key(owner: WebContents, id: unknown): string {
    if (typeof id !== 'string' || !id || id.length > 128)
      throw new Error('Invalid browser view ID');
    return owner.id + ':' + id;
  }
  private url(value: unknown): string {
    if (typeof value !== 'string' || value.length > 1048576) throw new Error('Invalid browser URL');
    const url = new URL(value);
    if (
      !['https:', 'http:', 'data:'].includes(url.protocol) ||
      (url.protocol === 'data:' && !value.startsWith('data:text/html'))
    )
      throw new Error('Unsupported browser URL');
    return url.href;
  }
  create(ownerContents: WebContents, id: string, src: unknown): Promise<void> {
    const key = this.key(ownerContents, id),
      url = this.url(src);
    if (this.records.has(key)) throw new Error('Duplicate browser view');
    const owner = BrowserWindow.fromWebContents(ownerContents);
    if (!owner || owner.isDestroyed()) throw new Error('Browser overlay owner unavailable');
    if ([...this.records.values()].filter((r) => r.owner === owner).length >= 8)
      throw new Error('Too many browser views');
    const window = new BaseWindow({
      parent: owner,
      frame: false,
      thickFrame: false,
      show: false,
      skipTaskbar: true,
      resizable: false,
      minimizable: false,
      maximizable: false,
      hasShadow: false,
      width: 1,
      height: 1,
      title: 'NativeBrowserView',
    });
    const view = new WebContentsView({
      webPreferences: { nodeIntegration: false, contextIsolation: true, sandbox: true },
    });
    window.contentView.addChildView(view);
    view.setVisible(false);
    const record: BrowserView = {
      id,
      owner,
      view,
      window,
      rect: null,
      ready: false,
      cleanup: () => {},
    };
    this.records.set(key, record);
    const send = (event: Omit<BrowserViewEvent, 'id'>) => {
      if (!ownerContents.isDestroyed()) this.emit(ownerContents, { id, ...event });
    };
    const update = () => this.measure(record);
    const retire = () => this.release(ownerContents, id);
    const navigate = (details: { isMainFrame: boolean; isSameDocument: boolean }) => {
      if (details.isMainFrame && !details.isSameDocument) retire();
    };
    const ownerEvents = ['move', 'resize', 'show', 'hide', 'minimize', 'restore'] as const;
    for (const event of ownerEvents) (owner as EventEmitter).on(event, update);
    ownerContents.on('did-start-navigation', navigate);
    ownerContents.once('destroyed', retire);
    ownerContents.once('render-process-gone', retire);
    record.cleanup = () => {
      for (const event of ownerEvents) (owner as EventEmitter).removeListener(event, update);
      ownerContents.removeListener('did-start-navigation', navigate);
      ownerContents.removeListener('destroyed', retire);
      ownerContents.removeListener('render-process-gone', retire);
    };
    view.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
    view.webContents.on('will-navigate', (event) => {
      try {
        this.url(event.url);
      } catch {
        event.preventDefault();
      }
    });
    view.webContents.on('will-redirect', (event) => {
      try {
        this.url(event.url);
      } catch {
        event.preventDefault();
      }
    });
    view.webContents.on('did-start-navigation', (details) => {
      if (details.isMainFrame && !details.isSameDocument) {
        record.ready = false;
        update();
      }
    });
    view.webContents.on('did-finish-load', () => {
      record.ready = true;
      send({ type: 'loaded' });
      update();
    });
    view.webContents.on('did-navigate', (_event, url) => send({ type: 'navigated', url }));
    view.webContents.on('did-navigate-in-page', (_event, url) => send({ type: 'navigated', url }));
    view.webContents.on('did-fail-load', (_event, code, message, _url, isMainFrame) => {
      if (isMainFrame && code !== -3) send({ type: 'error', message });
    });
    view.webContents.on('render-process-gone', () => {
      send({ type: 'error', message: '网页进程已退出' });
      retire();
    });
    window.once('closed', () => {
      this.records.delete(key);
      record.cleanup();
      if (!view.webContents.isDestroyed()) view.webContents.close();
    });
    return view.webContents.loadURL(url).catch((error) => {
      this.release(ownerContents, id);
      throw error;
    });
  }
  load(owner: WebContents, id: string, src: unknown): Promise<void> {
    const record = this.record(owner, id),
      url = this.url(src);
    record.ready = false;
    this.measure(record);
    return record.view.webContents.loadURL(url);
  }
  setRect(owner: WebContents, id: string, value: unknown): void {
    const record = this.record(owner, id);
    if (!value || typeof value !== 'object') throw new Error('Invalid browser view rect');
    const r = value as BrowserViewRect;
    if (
      ![r.x, r.y, r.width, r.height].every(Number.isFinite) ||
      r.width < 0 ||
      r.height < 0 ||
      r.width > 16384 ||
      r.height > 16384 ||
      Math.abs(r.x) > 1048576 ||
      Math.abs(r.y) > 1048576 ||
      typeof r.visible !== 'boolean'
    )
      throw new Error('Invalid browser view rect');
    record.rect = { x: r.x, y: r.y, width: r.width, height: r.height, visible: r.visible };
    this.measure(record);
  }
  release(owner: WebContents, id: string): void {
    const key = this.key(owner, id),
      record = this.records.get(key);
    if (!record) return;
    this.records.delete(key);
    record.cleanup();
    if (!record.view.webContents.isDestroyed()) record.view.webContents.close();
    if (!record.window.isDestroyed()) record.window.destroy();
    this.native.refresh();
  }
  stop(): void {
    for (const record of [...this.records.values()])
      this.release(record.owner.webContents, record.id);
  }
  private record(owner: WebContents, id: string): BrowserView {
    const record = this.records.get(this.key(owner, id));
    if (!record) throw new Error('Unknown browser view');
    return record;
  }
  private measure(record: BrowserView): void {
    const { owner, window, view, rect } = record;
    if (window.isDestroyed() || owner.isDestroyed()) return;
    if (
      !record.ready ||
      !rect?.visible ||
      rect.width <= 0 ||
      rect.height <= 0 ||
      !owner.isVisible() ||
      owner.isMinimized()
    ) {
      view.setVisible(false);
      window.hide();
      return;
    }
    const origin = owner.getContentBounds(),
      zoom = owner.webContents.getZoomFactor();
    const bounds = {
      x: origin.x + Math.round(rect.x * zoom),
      y: origin.y + Math.round(rect.y * zoom),
      width: Math.max(1, Math.round(rect.width * zoom)),
      height: Math.max(1, Math.round(rect.height * zoom)),
    };
    window.setBounds(bounds);
    view.setBounds({ x: 0, y: 0, width: bounds.width, height: bounds.height });
    if (!window.isVisible()) {
      window.showInactive();
      window.moveTop();
      this.native.refresh();
    }
    view.setVisible(true);
  }
}
