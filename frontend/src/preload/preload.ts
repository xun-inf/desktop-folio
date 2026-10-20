import { contextBridge, ipcRenderer, sharedTexture } from 'electron';
import { IpcClient } from '../ipc/client.js';
import { configurationContract } from '../main/infrastructure/configuration/contract.js';
import {
  mediaContract,
  nativeWindowContract,
  streamPreviewContract,
  type StreamPreviewFrameMeta,
} from '../main/mediaService/contract.js';
import { testModuleContract } from '../main/bizModules/contract.js';
import { windowsContract, browserViewContract } from '../main/window/contract.js';

const errorCallbacks = new Set<(error: { message: string }) => void>();
const client = new IpcClient(ipcRenderer, (error) => {
  const message = error instanceof Error ? error.message : String(error);
  console.error('Folio IPC subscription/callback failed:', message);
  for (const callback of [...errorCallbacks]) {
    try {
      callback({ message });
    } catch (failure) {
      console.error('Folio IPC error callback failed:', failure);
    }
  }
});
const configuration = client.bind(configurationContract),
  media = client.bind(mediaContract);
const windows = client.bind(windowsContract),
  browserView = client.bind(browserViewContract);
const streamPreview = client.bind(streamPreviewContract),
  testModule = client.bind(testModuleContract);
type FrameCallback = (frame: VideoFrame, meta: StreamPreviewFrameMeta) => void | Promise<void>;
const frameCallbacks = new Map<number, FrameCallback>();
let closed = false;
// Textures keep their dedicated transport and explicit frame/token ownership.
if (sharedTexture && typeof sharedTexture.setSharedTextureReceiver === 'function') {
  sharedTexture.setSharedTextureReceiver(async (received, meta: StreamPreviewFrameMeta) => {
    const callback = meta && frameCallbacks.get(meta.id);
    if (!callback) {
      received.importedSharedTexture.release();
      return;
    }
    const frame = received.importedSharedTexture.getVideoFrame();
    try {
      await callback(frame, meta);
    } catch {
      try {
        frame.close();
      } catch {
        /* Already closed. */
      }
    }
  });
}
async function subscribeStreamPreview(
  id: number,
  onFrame: FrameCallback,
): Promise<() => Promise<void>> {
  if (closed) throw new Error('Page document retired');
  if (typeof onFrame !== 'function') throw new TypeError('Frame callback must be a function');
  if (frameCallbacks.has(id)) throw new Error('Stream preview already has a consumer');
  frameCallbacks.set(id, onFrame);
  try {
    await streamPreview.bind(id);
  } catch (error) {
    frameCallbacks.delete(id);
    throw error;
  }
  let disposed = false;
  return async () => {
    if (disposed) return;
    disposed = true;
    frameCallbacks.delete(id);
    await streamPreview.unbind(id).catch(() => {});
  };
}
const folioMain = {
  // Function disposer properties are not copied by contextBridge. Errors have an explicit observable API.
  onIpcError: (callback: (error: { message: string }) => void): (() => void) => {
    if (typeof callback !== 'function')
      throw new TypeError('IPC error callback must be a function');
    if (closed) throw new Error('Page document retired');
    errorCallbacks.add(callback);
    return () => {
      errorCallbacks.delete(callback);
    };
  },
  configuration: {
    ...configuration,
    onLanguageChanged: (
      callback: Parameters<
        typeof client.subscribe<typeof configurationContract, 'languageChanged'>
      >[2],
    ): (() => void) => client.subscribe(configurationContract, 'languageChanged', callback),
  },
  windows: {
    ...windows,
    ready: (): void => {
      void windows.ready().catch((error) => console.error('Renderer ready failed:', error));
    },
    onState: (
      callback: Parameters<typeof client.subscribe<typeof windowsContract, 'state'>>[2],
    ): (() => void) => client.subscribe(windowsContract, 'state', callback),
  },
  browserView: {
    ...browserView,
    onEvent: (
      callback: Parameters<typeof client.subscribe<typeof browserViewContract, 'event'>>[2],
    ): (() => void) => client.subscribe(browserViewContract, 'event', callback),
  },
};
const folioMediaService = {
  media: {
    ...media,
    subscribeStatus: (
      callback: Parameters<typeof client.subscribe<typeof mediaContract, 'status'>>[2],
    ): (() => void) => client.subscribe(mediaContract, 'status', callback),
  },
  nativeWindowService: client.bind(nativeWindowContract),
  streamPreviewService: { ...streamPreview, subscribe: subscribeStreamPreview },
};
const folioBizModules = {
  testModule: {
    ...testModule,
    subscribe: (
      event: 'changed',
      callback: (value: string) => void,
    ): { dispose(): void; ready: Promise<void> } => {
      const dispose = client.subscribe(testModuleContract, event, callback);
      return { dispose, ready: dispose.ready };
    },
  },
};
function close(): void {
  if (closed) return;
  closed = true;
  frameCallbacks.clear();
  errorCallbacks.clear();
  client.close();
}
window.addEventListener('pagehide', close, { once: true });
window.addEventListener('unload', close, { once: true });
contextBridge.exposeInMainWorld('folioMain', folioMain);
contextBridge.exposeInMainWorld('folioMediaService', folioMediaService);
contextBridge.exposeInMainWorld('folioBizModules', folioBizModules);
export type FolioMainApi = typeof folioMain;
export type FolioMediaServiceApi = typeof folioMediaService;
export type FolioBizModulesApi = typeof folioBizModules;
