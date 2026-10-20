import { createRequire } from 'node:module';

// node-media-service addon 契约（与 core/node-media-service/index.d.ts 保持一致的本地镜像；
// TS 构建域约束（rootDir=src）不允许直接 import 仓库外类型，后续可改为生成 TS 契约）。
// createRequire 用于加载 .node 原生模块。

const require = createRequire(import.meta.url);

export interface Disposable {
  dispose(): void;
}

export interface Status {
  connected: boolean;
  message: string;
  phase: 'idle' | 'starting' | 'handshaking' | 'ready' | 'disconnected' | 'stopped';
  pid: number;
  session: string;
}

export interface MediaServiceOptions {
  executable: string;
  pipeName?: string;
  logPath?: string;
  startupTimeoutMs?: number;
  shutdownTimeoutMs?: number;
  arguments?: string[];
}

export interface Rect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface FrameEvent {
  id: number;
  token: bigint;
  handle?: Buffer;
  width: number;
  height: number;
  timestampUs: number;
}

export interface NativeWindowServiceApi {
  create(id: number, host: bigint, hostVisible: boolean): Promise<void>;
  refresh(id: number, host: bigint, hostVisible: boolean): Promise<void>;
  releaseAll(): Promise<void>;
  release(id: number): Promise<void>;
  setRect(id: number, x: number, y: number, width: number, height: number): Promise<void>;
  setVisible(id: number, visible: boolean): Promise<void>;
}

export interface StreamPreviewServiceApi {
  createStreamPreview(id: number): Promise<void>;
  releaseStreamPreview(id: number): Promise<void>;
  releaseVideoFrame(id: number, token: bigint): Promise<void>;
  subscribeFrame(callback: (frame: FrameEvent) => void): Disposable;
}

export interface MediaServiceApi {
  initialize(): Promise<void>;
  getStatus(): Status;
  subscribeStatus(callback: (status: Status) => void): Disposable;
  nativeWindowService: NativeWindowServiceApi;
  streamPreviewService: StreamPreviewServiceApi;
  stop(): Promise<void>;
}

export function loadMediaService(
  modulePath: string,
  options: MediaServiceOptions,
): MediaServiceApi {
  const addon = require(modulePath) as {
    createMediaService(options: MediaServiceOptions): MediaServiceApi;
  };
  return addon.createMediaService(options);
}
