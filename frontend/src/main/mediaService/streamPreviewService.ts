import {
  sharedTexture,
  type SharedTextureImported,
  type WebContents,
  type WebFrameMain,
} from 'electron';
import { logger } from '../logger.js';
import type { Disposable, FrameEvent, StreamPreviewServiceApi } from './addon.js';
import type { InvokeContext } from './types.js';

// 帧元数据（id/token）随 sendSharedTexture 的第二参送达 renderer；VideoFrame 由 preload 构造。
export interface StreamPreviewFrameMeta {
  id: number;
  token: bigint;
  width: number;
  height: number;
  timestampUs: number;
}

interface Binding {
  id: number;
  target: WebContents;
  frame: WebFrameMain;
  frameToken: string;
  processId: number;
  routingId: number;
  outstanding: Set<bigint>;
  quiescing: boolean;
  retired: boolean;
  drains: Set<() => void>;
  dispose(): void;
}

// 业务门面：addon 的 subscribeFrame 是单监听者，本门面持有全部 preview 绑定并按 id 路由；
// 帧由主进程 importSharedTexture 后经 sharedTexture 通道发送，renderer 消费后显式归还（方案 B）。
export class StreamPreviewService {
  private readonly bindings = new Map<number, Binding>();
  private readonly known = new Set<number>();
  private readonly drains = new Map<WebContents, Promise<void>>();
  private subscription: Disposable | null = null;
  private stopped = false;

  constructor(private readonly native: () => StreamPreviewServiceApi) {}

  async bind(id: number, context: InvokeContext): Promise<void> {
    if (this.stopped) throw new Error('Stream preview has stopped');
    if (!Number.isInteger(id) || id < 0 || id > 0xffffffff || context.sender.isDestroyed())
      throw new Error('Invalid stream preview binding');
    const existing = this.bindings.get(id);
    if (existing) {
      if (existing.target === context.sender && !existing.retired) return;
      throw new Error('Stream preview already has a consumer');
    }
    await this.native().createStreamPreview(id);
    if (context.sender.isDestroyed()) {
      await this.native()
        .releaseStreamPreview(id)
        .catch(() => {});
      throw new Error('Renderer changed during preview bind');
    }
    const pageFrame = context.frame ?? context.sender.mainFrame;
    if (pageFrame.detached || pageFrame !== context.sender.mainFrame) {
      await this.native()
        .releaseStreamPreview(id)
        .catch(() => {});
      throw new Error('Renderer changed during preview bind');
    }
    const binding: Binding = {
      id,
      target: context.sender,
      frame: pageFrame,
      frameToken: pageFrame.frameToken,
      processId: pageFrame.processId,
      routingId: pageFrame.routingId,
      outstanding: new Set(),
      quiescing: false,
      retired: false,
      drains: new Set(),
      dispose: () => {},
    };
    const dispose = () => this.retire(id, binding);
    const navigate = (
      _event: Electron.Event,
      _url: string,
      _httpCode: number,
      _httpStatus: string,
      isMainFrame: boolean,
      frameProcessId: number,
      frameRoutingId: number,
    ): void => {
      if (
        isMainFrame &&
        (frameProcessId !== binding.processId ||
          frameRoutingId !== binding.routingId ||
          !this.sameDocument(binding))
      )
        dispose();
    };
    context.sender.once('destroyed', dispose);
    context.sender.on('render-process-gone', dispose);
    context.sender.on('did-frame-navigate', navigate);
    binding.dispose = () => {
      context.sender.off('destroyed', dispose);
      context.sender.off('render-process-gone', dispose);
      context.sender.off('did-frame-navigate', navigate);
    };
    this.bindings.set(id, binding);
    this.known.add(id);
    this.ensureSubscription();
  }

  async unbind(id: number, context: InvokeContext): Promise<void> {
    const binding = this.bindings.get(id);
    if (!binding || binding.retired || binding.target !== context.sender) return;
    this.retire(id, binding);
  }

  async releaseVideoFrame(id: number, token: bigint, context?: InvokeContext): Promise<void> {
    if (this.stopped) throw new Error('Stream preview has stopped');
    if (!Number.isInteger(id) || id < 0 || id > 0xffffffff || typeof token !== 'bigint')
      throw new Error('Invalid video frame release');
    // C++ 侧 releaseFrame 幂等：即使绑定已回收也允许兜底归还，避免与回收竞态。
    if (!this.known.has(id)) throw new Error('Unknown stream preview: ' + id);
    const owner = this.bindings.get(id);
    if (
      context &&
      owner &&
      (owner.target !== context.sender ||
        owner.frame !== context.frame ||
        !this.sameDocument(owner))
    )
      throw new Error('Stream preview consumer required');
    await this.native().releaseVideoFrame(id, token);
  }

  drainDocument(
    target: WebContents,
    options: { timeoutMs?: number } = {},
  ): Promise<void> | undefined {
    const existing = this.drains.get(target);
    if (existing) return existing;
    const affected = [...this.bindings.values()].filter((binding) => binding.target === target);
    if (!affected.length) return undefined;
    const timeoutMs = options.timeoutMs ?? 12000;
    if (!Number.isInteger(timeoutMs) || timeoutMs < 1 || timeoutMs > 15000)
      throw new Error('Invalid preview drain timeout');
    // 导航期间旧文档不再接收新帧；等已转发帧全部归还（allReferencesReleased）后回收绑定。
    for (const binding of affected) binding.quiescing = true;
    const verify = () => {
      if (this.stopped || target.isDestroyed() || target.isCrashed())
        throw new Error('Preview document unavailable during drain');
      for (const binding of affected) {
        if (!this.sameDocument(binding)) throw new Error('Preview document changed during drain');
      }
    };
    const finish = () => {
      verify();
      const failures: unknown[] = [];
      for (const binding of affected) {
        try {
          this.retire(binding.id, binding);
        } catch (error) {
          failures.push(error);
        }
      }
      if (failures.length) throw new AggregateError(failures, 'Preview drain unsubscribe failed');
    };
    if (affected.every((binding) => binding.outstanding.size === 0)) {
      finish();
      return;
    }
    let timer: NodeJS.Timeout | undefined;
    let completing = false;
    let wake = () => {};
    const pending = new Promise<void>((resolve, reject) => {
      wake = () => {
        if (completing) return;
        try {
          verify();
          if (affected.every((binding) => binding.outstanding.size === 0)) {
            completing = true;
            finish();
            resolve();
          }
        } catch (error) {
          reject(error);
        }
      };
      for (const binding of affected) binding.drains.add(wake);
      timer = setTimeout(() => reject(new Error('Preview texture drain timed out')), timeoutMs);
      wake();
    });
    const result = pending.finally(() => {
      if (timer !== undefined) clearTimeout(timer);
      for (const binding of affected) binding.drains.delete(wake);
      if (this.drains.get(target) === result) this.drains.delete(target);
    });
    this.drains.set(target, result);
    return result;
  }

  stop(): void {
    this.stopped = true;
    this.subscription?.dispose();
    this.subscription = null;
    const failures: unknown[] = [];
    for (const [id, binding] of [...this.bindings]) {
      try {
        this.retire(id, binding);
      } catch (error) {
        failures.push(error);
      }
    }
    if (failures.length) throw new AggregateError(failures, 'Stream preview cleanup failed');
  }

  private ensureSubscription(): void {
    if (this.subscription) return;
    this.subscription = this.native().subscribeFrame((frame) => {
      try {
        this.dispatch(frame);
      } catch (error) {
        logger.error('video frame dispatch failed:', error);
      }
    });
  }

  private dispatch(frame: FrameEvent): void {
    const binding = this.bindings.get(frame.id);
    if (!binding || binding.retired || binding.quiescing || !frame.handle) {
      void this.native()
        .releaseVideoFrame(frame.id, frame.token)
        .catch((error) => logger.error('video frame release failed:', error));
      return;
    }
    const meta: StreamPreviewFrameMeta = {
      id: frame.id,
      token: frame.token,
      width: frame.width,
      height: frame.height,
      timestampUs: frame.timestampUs,
    };
    let imported: SharedTextureImported | null = null;
    try {
      imported = sharedTexture.importSharedTexture({
        textureInfo: {
          handle: { ntHandle: frame.handle },
          pixelFormat: 'bgra',
          codedSize: { width: frame.width, height: frame.height },
          timestamp: frame.timestampUs,
        },
        allReferencesReleased: () => this.resolved(binding, frame.token),
      });
      binding.outstanding.add(frame.token);
      void sharedTexture
        .sendSharedTexture({ frame: binding.frame, importedSharedTexture: imported }, meta)
        .catch((error) => {
          binding.outstanding.delete(frame.token);
          logger.error('video texture transfer failed:', error);
          void this.native()
            .releaseVideoFrame(frame.id, frame.token)
            .catch((releaseError) => logger.error('video frame release failed:', releaseError));
        })
        .finally(() => {
          try {
            imported?.release();
          } catch {
            /* 已释放 */
          }
        });
    } catch (error) {
      binding.outstanding.delete(frame.token);
      logger.error('video texture import failed:', error);
      void this.native()
        .releaseVideoFrame(frame.id, frame.token)
        .catch((releaseError) => logger.error('video frame release failed:', releaseError));
    }
  }

  private resolved(binding: Binding, token: bigint): void {
    binding.outstanding.delete(token);
    for (const wake of [...binding.drains]) {
      try {
        wake();
      } catch {
        /* drain 失败由调用方处理 */
      }
    }
  }

  private retire(id: number, binding: Binding): void {
    if (this.bindings.get(id) !== binding) return;
    this.bindings.delete(id);
    binding.retired = true;
    try {
      binding.dispose();
    } catch (error) {
      logger.error('stream preview binding cleanup failed:', error);
    }
    for (const token of [...binding.outstanding]) {
      void this.native()
        .releaseVideoFrame(id, token)
        .catch((error) => logger.error('video frame release failed:', error));
    }
    binding.outstanding.clear();
    for (const wake of [...binding.drains]) {
      try {
        wake();
      } catch {
        /* drain 失败由调用方处理 */
      }
    }
    void this.native()
      .releaseStreamPreview(id)
      .catch((error) => logger.error('stream preview release failed:', error));
  }

  private sameDocument(binding: Binding): boolean {
    try {
      return (
        !binding.target.isDestroyed() &&
        binding.target.mainFrame === binding.frame &&
        binding.frame.frameToken === binding.frameToken &&
        binding.frame.processId === binding.processId &&
        binding.frame.routingId === binding.routingId
      );
    } catch {
      return false;
    }
  }
}
