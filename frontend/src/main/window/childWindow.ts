import { BrowserWindow } from 'electron';
import { logger } from '../logger.js';
import { popupWindowMgr } from './popupWindow.js';
import { mainWindow } from './mainWindow.js';

interface ChildWindowOptions {
  pageUrl: string;
  preload: string;
}

class ChildWindow {
  private window: BrowserWindow | null = null;
  private initialization: ChildWindowOptions | null = null;

  initialize(options: ChildWindowOptions): void {
    if (this.initialization !== null) throw new Error('ChildWindow is already initialized');
    this.initialization = { ...options };
  }

  get target(): BrowserWindow | null {
    return this.window !== null && !this.window.isDestroyed() ? this.window : null;
  }

  create(): void {
    if (this.target !== null) return;
    const options = this.initialization;
    if (options === null) throw new Error('ChildWindow must be initialized before use');
    const parent = mainWindow.target;
    if (parent === null) throw new Error('MainWindow must be created before ChildWindow');
    const target = new BrowserWindow({
      parent,
      frame: false,
      skipTaskbar: true,
      width: 800,
      height: 600,
      minWidth: 480,
      minHeight: 320,
      show: false,
      title: '设置',
      backgroundColor: '#0f141a',
      webPreferences: {
        preload: options.preload,
        nodeIntegration: false,
        contextIsolation: true,
        sandbox: true,
      },
    });
    target.setMenu(null);
    this.window = target;
    target.once('closed', () => {
      this.window = null;
    });
    popupWindowMgr.register('child', target.id, undefined, options.pageUrl);
    const url = new URL(options.pageUrl);
    url.searchParams.set('id', 'child');
    target.loadURL(url.href).catch((error) => {
      logger.error('child renderer load failed:', error);
    });
  }
}

export const childWindow = new ChildWindow();
