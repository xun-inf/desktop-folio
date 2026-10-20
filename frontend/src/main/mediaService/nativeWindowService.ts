import type { BrowserWindow } from 'electron';
import { logger } from '../logger.js';
import type { NativeWindowServiceApi } from './addon.js';

// 业务门面：key→id 映射收在 Electron 主进程（C++ 侧已删除 key→id 门面），
// 主窗口宿主移动/缩放/显隐事件统一刷新所有已注册原生窗口。
export interface NativeWindowFacade {
  create(key: string): Promise<void>;
  setRect(key: string, rect: NativeWindowRectInput): Promise<void>;
  setVisible(key: string, visible: boolean): Promise<void>;
  release(key: string): Promise<void>;
  releaseAll(): Promise<void>;
  refresh(): Promise<void>;
  list(): Array<{ key: string; id: number }>;
}

export interface NativeWindowRectInput {
  x: number;
  y: number;
  width: number;
  height: number;
  dpr: number;
}

interface Record {
  key: string;
  id: number;
}

const WINDOW_EVENTS = [
  'move',
  'resize',
  'show',
  'hide',
  'minimize',
  'restore',
  'maximize',
  'unmaximize',
  'enter-full-screen',
  'leave-full-screen',
] as const;

export class NativeWindowService implements NativeWindowFacade {
  private readonly records = new Map<string, Record>();
  private host: BrowserWindow | null = null;
  private loading = true;
  private stopped = false;
  private nextId = 1;

  constructor(private readonly native: () => NativeWindowServiceApi) {}

  bindMainWindow(window: BrowserWindow): void {
    if (this.stopped) throw new Error('Native window service has stopped');
    if (this.host === window) return;
    if (this.host) throw new Error('Main window already bound');
    this.host = window;
    const refresh = () => {
      void this.refresh().catch((error) => logger.error('native window refresh failed:', error));
    };
    const emitter = window as unknown as {
      on(event: string, listener: (...args: unknown[]) => void): void;
    };
    for (const event of WINDOW_EVENTS) emitter.on(event, refresh);
    window.webContents.on('did-start-navigation', (details) => {
      if (details.isMainFrame && !details.isSameDocument) {
        this.loading = true;
        void this.releaseAll().catch((error) =>
          logger.error('native window release failed:', error),
        );
      }
    });
    window.webContents.on('did-finish-load', () => {
      this.loading = false;
      this.refresh();
    });
    window.webContents.on('render-process-gone', () => {
      this.loading = true;
      void this.releaseAll().catch((error) => logger.error('native window release failed:', error));
    });
    window.once('closed', () => {
      void this.releaseAll().catch((error) => logger.error('native window release failed:', error));
      this.host = null;
    });
  }

  private hostValues(): [bigint, boolean] {
    const host = this.host;
    if (!host || host.isDestroyed()) throw new Error('Main window is unavailable');
    const buffer = host.getNativeWindowHandle();
    return [
      buffer.length >= 8 ? buffer.readBigUInt64LE() : BigInt(buffer.readUInt32LE()),
      !this.loading && host.isVisible() && !host.isMinimized(),
    ];
  }

  async create(key: string): Promise<void> {
    if (this.stopped || this.records.has(key)) return;
    this.validateKey(key);
    const id = this.nextId++;
    if (id > 0xffffffff) throw new Error('Native window id exhausted');
    this.records.set(key, { key, id });
    const [host, visible] = this.hostValues();
    await this.native().create(id, host, visible);
  }

  async setRect(key: string, rect: NativeWindowRectInput): Promise<void> {
    if (this.stopped) return;
    const record = this.requireRecord(key);
    if (
      !rect ||
      ![rect.x, rect.y, rect.width, rect.height, rect.dpr].every(Number.isFinite) ||
      rect.width < 0 ||
      rect.height < 0 ||
      rect.width > 16384 ||
      rect.height > 16384 ||
      Math.abs(rect.x) > 1048576 ||
      Math.abs(rect.y) > 1048576 ||
      rect.dpr <= 0 ||
      rect.dpr > 16
    )
      throw new Error('Invalid native window rect');
    const [x, y, width, height] = [rect.x, rect.y, rect.width, rect.height].map((value) =>
      Math.round(value * rect.dpr),
    );
    await this.native().setRect(record.id, x, y, width, height);
  }

  async setVisible(key: string, visible: boolean): Promise<void> {
    if (this.stopped) return;
    if (typeof visible !== 'boolean') throw new Error('Invalid native window visibility');
    const record = this.requireRecord(key);
    await this.native().setVisible(record.id, visible);
  }

  async release(key: string): Promise<void> {
    if (this.stopped) return;
    const record = this.records.get(key);
    if (!record) return;
    this.records.delete(key);
    await this.native().release(record.id);
  }

  async releaseAll(): Promise<void> {
    if (this.records.size === 0) return;
    const ids = [...this.records.values()].map((record) => record.id);
    this.records.clear();
    const results = await Promise.allSettled(ids.map((id) => this.native().release(id)));
    for (const result of results)
      if (result.status === 'rejected')
        logger.error('native window releaseAll failed:', result.reason);
  }

  async refresh(): Promise<void> {
    if (this.stopped || this.loading || !this.host || this.host.isDestroyed()) return;
    const [host, visible] = this.hostValues();
    const results = await Promise.allSettled(
      [...this.records.values()].map((record) => this.native().refresh(record.id, host, visible)),
    );
    for (const result of results)
      if (result.status === 'rejected')
        logger.error('native window refresh failed:', result.reason);
  }

  list(): Array<{ key: string; id: number }> {
    return [...this.records.values()].map((record) => ({ key: record.key, id: record.id }));
  }

  stop(): Promise<void> {
    this.stopped = true;
    return this.releaseAll();
  }

  private validateKey(key: string): void {
    if (typeof key !== 'string' || !/^[a-zA-Z0-9_.-]{1,128}$/.test(key))
      throw new Error('Invalid native window key');
  }

  private requireRecord(key: string): Record {
    const record = this.records.get(key);
    if (!record) throw new Error('Unknown native window: ' + key);
    return record;
  }
}
