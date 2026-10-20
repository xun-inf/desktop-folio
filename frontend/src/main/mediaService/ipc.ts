import type { IpcServer } from '../../ipc/server.js';
import type { MediaServiceContainer } from './mediaService.js';
import { mediaContract, nativeWindowContract, streamPreviewContract } from './contract.js';
export function registerMediaIpc(server: IpcServer, service: MediaServiceContainer): void {
  server.register(
    mediaContract,
    {
      getStatus: () => service.media.getStatus(),
      initialize: () => service.ensureConnected(),
      stop: () => service.stop(),
    },
    {
      status: (_caller, emit) => {
        const dispose = service.media.onStatus(emit);
        emit(service.media.getStatus());
        return dispose;
      },
    },
  );
  server.register(nativeWindowContract, {
    create: (_caller, key) => service.nativeWindow.create(key),
    setRect: (_caller, key, rect) => service.nativeWindow.setRect(key, rect),
    setVisible: (_caller, key, visible) => service.nativeWindow.setVisible(key, visible),
    release: (_caller, key) => service.nativeWindow.release(key),
    releaseAll: () => service.nativeWindow.releaseAll(),
    refresh: () => service.nativeWindow.refresh(),
  });
  server.register(streamPreviewContract, {
    bind: (caller, id) => service.streamPreview.bind(id, caller),
    unbind: (caller, id) => service.streamPreview.unbind(id, caller),
    releaseVideoFrame: (caller, id, token) =>
      service.streamPreview.releaseVideoFrame(id, token, caller),
  });
}
