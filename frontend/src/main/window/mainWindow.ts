import { BrowserWindow, type IpcMainEvent, type IpcMainInvokeEvent } from 'electron';
import fs from 'node:fs';
import path from 'node:path';

import type { AppPaths } from '../appPaths.js';
import { mediaService } from '../mediaService/mediaService.js';
import { logger } from '../logger.js';
import { popupWindowMgr } from './popupWindow.js';

interface MainWindowOptions {
  paths: AppPaths;
  preload: string;
  pageUrl: string;
  devtoolsEnabled: boolean;
  canClose(): boolean;
  onClose(): void;
}

class MainWindow {
  private window: BrowserWindow | null = null;
  private rendererReady = false;
  private stopping = false;

  private initialization: MainWindowOptions | null = null;

  initialize(options: MainWindowOptions): void {
    if (this.initialization !== null) throw new Error('MainWindow is already initialized');
    this.initialization = { ...options, paths: { ...options.paths } };
  }

  private get options(): MainWindowOptions {
    if (this.initialization === null) throw new Error('MainWindow must be initialized before use');
    return this.initialization;
  }

  get target(): BrowserWindow | null {
    return this.window !== null && !this.window.isDestroyed() ? this.window : null;
  }

  get ready(): boolean {
    return this.rendererReady && this.target !== null;
  }

  create(): void {
    if (this.stopping) throw new Error('MainWindow has been stopped');
    if (this.target !== null) return;
    const { paths, preload } = this.options;
    const iconPath = path.join(paths.configDir, 'app.ico');
    const target = new BrowserWindow({
      width: 1100,
      height: 720,
      minWidth: 640,
      minHeight: 400,
      frame: false,
      show: false,
      title: 'Folio',
      backgroundColor: '#0f141a',
      icon: fs.existsSync(iconPath) ? iconPath : undefined,
      webPreferences: {
        preload,
        contextIsolation: true,
        nodeIntegration: false,
        sandbox: true,
      },
    });
    this.window = target;
    popupWindowMgr.register('main', target.id, undefined, this.options.pageUrl);
    mediaService.bindMainWindow(target);
    target.once('ready-to-show', () => {
      if (!target.isDestroyed()) target.show();
    });
    target.on('close', (event) => {
      if (this.options.canClose()) return;
      event.preventDefault();
      this.options.onClose();
    });
    target.on('closed', () => {
      this.stop();
      this.window = null;
    });

    target.webContents.on('did-start-navigation', (details) => {
      if (details.isMainFrame && !details.isSameDocument) this.markRendererUnavailable();
    });
    target.webContents.on('render-process-gone', (_event, details) => {
      logger.error('renderer gone: ' + details.reason);
      this.markRendererUnavailable();
    });
    target.webContents.on('console-message', (details) => {
      const message = 'renderer: ' + details.message;
      switch (details.level) {
        case 'error':
          logger.error(message);
          break;
        case 'warning':
          logger.warning(message);
          break;
        case 'debug':
          logger.debug(message);
          break;
        default:
          logger.info(message);
      }
    });
    target.webContents.on('did-finish-load', () => {
      if (this.stopping) return;
      this.rendererReady = true;
      this.onRendererReady();
    });
    target.webContents.on('before-input-event', (event, input) => {
      if (!this.options.devtoolsEnabled || input.type !== 'keyDown') return;
      const inspect =
        (input.control || input.meta) && input.shift && input.key.toLowerCase() === 'i';
      if (input.key !== 'F12' && !inspect) return;
      event.preventDefault();
      if (target.webContents.isDevToolsOpened()) {
        target.webContents.closeDevTools();
      } else {
        target.webContents.openDevTools({ mode: 'detach' });
      }
    });
    const url = new URL(this.options.pageUrl);
    url.searchParams.set('id', 'main');
    target.loadURL(url.href).catch((error: unknown) => {
      logger.error('renderer load failed:', error);
      this.markRendererUnavailable();
    });
    if (this.options.devtoolsEnabled) target.webContents.openDevTools({ mode: 'detach' });
  }

  stop(): void {
    this.stopping = true;
    this.markRendererUnavailable();
  }

  private async onRendererReady(): Promise<void> {
    if (this.stopping) return;
    try {
      await mediaService.ensureConnected();
    } catch (error) {
      logger.error('media-service connection failed:', error);
    }
  }

  accepts(event: IpcMainEvent | IpcMainInvokeEvent): boolean {
    const target = this.target;
    return target !== null && event.sender === target.webContents;
  }

  private markRendererUnavailable(): void {
    this.rendererReady = false;
  }
}

export const mainWindow = new MainWindow();
