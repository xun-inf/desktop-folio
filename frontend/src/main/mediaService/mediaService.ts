import type { BrowserWindow } from 'electron';
import type { MediaServiceApi, Status } from './addon.js';
import { MediaStatusService } from './mediaStatusService.js';
import { NativeWindowService } from './nativeWindowService.js';
import { StreamPreviewService } from './streamPreviewService.js';

// mediaService 容器：主进程单例，统一持有 node-media-service 各服务对象。
// 生命周期和具名业务门面；IPC 绑定独立放在同目录 ipc.ts。
export class MediaServiceContainer {
  private mediaStatus: MediaStatusService | null = null;
  private native: MediaServiceApi | null = null;
  private stopping = false;

  readonly nativeWindow: NativeWindowService;
  readonly streamPreview: StreamPreviewService;

  constructor() {
    // 门面延迟到 install 后才真正访问原生对象。
    this.nativeWindow = new NativeWindowService(() => this.requireNative().nativeWindowService);
    this.streamPreview = new StreamPreviewService(() => this.requireNative().streamPreviewService);
  }

  install(native: MediaServiceApi): void {
    if (this.native) throw new Error('MediaServiceContainer is already installed');
    this.native = native;
    this.mediaStatus = new MediaStatusService(native);
  }

  get media(): MediaStatusService {
    if (!this.mediaStatus) throw new Error('MediaServiceContainer must be installed before use');
    return this.mediaStatus;
  }

  requireNative(): MediaServiceApi {
    if (!this.native) throw new Error('MediaServiceContainer must be installed before use');
    return this.native;
  }

  get status(): Status {
    return this.requireNative().getStatus();
  }

  onStatus(handler: (status: Status) => void): () => void {
    const subscription = this.requireNative().subscribeStatus(handler);
    return () => subscription.dispose();
  }

  async ensureConnected(): Promise<boolean> {
    if (this.stopping) return false;
    try {
      await this.requireNative().initialize();
      return this.requireNative().getStatus().connected;
    } catch {
      return false;
    }
  }

  bindMainWindow(window: BrowserWindow): void {
    this.nativeWindow.bindMainWindow(window);
  }

  async stop(): Promise<void> {
    if (this.stopping) return;
    this.stopping = true;
    const failures: unknown[] = [];
    try {
      this.streamPreview.stop();
    } catch (error) {
      failures.push(error);
    }
    try {
      await this.nativeWindow.stop();
    } catch (error) {
      failures.push(error);
    }
    try {
      await this.native?.stop();
    } catch (error) {
      failures.push(error);
    }
    if (failures.length === 1) throw failures[0];
    if (failures.length > 1)
      throw new AggregateError(failures, 'MediaServiceContainer cleanup failed');
  }
}

export const mediaService = new MediaServiceContainer();
