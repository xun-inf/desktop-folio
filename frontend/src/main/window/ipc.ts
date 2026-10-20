import type { IpcServer } from '../../ipc/server.js';
import type { NativeWindowFacade } from '../mediaService/nativeWindowService.js';
import { windowsContract, browserViewContract, type WindowConfig } from './contract.js';
import { BrowserViewMgr } from './browserView.js';
import { popupWindowMgr } from './popupWindow.js';
export function registerWindowIpc(
  server: IpcServer,
  config: WindowConfig,
  native: NativeWindowFacade,
): () => void {
  popupWindowMgr.initialize(config);
  popupWindowMgr.setStateOutlet((sender, state) =>
    server.emit(windowsContract, 'state', sender, state),
  );
  const overlays = new BrowserViewMgr(native, (sender, event) =>
    server.emit(browserViewContract, 'event', sender, event),
  );
  server.register(
    windowsContract,
    {
      control: (caller, action) => popupWindowMgr.control(caller, action),
      ready: (caller) => popupWindowMgr.rendererReady(caller),
      getState: (caller) => ({
        maximized: popupWindowMgr.requireSender(caller).window.isMaximized(),
      }),
      showWindow: (_caller, options) => popupWindowMgr.showWindow(options),
      closeWindow: (_caller, id) => popupWindowMgr.closeWindow(id),
      closeAllWindow: () => popupWindowMgr.closeAllWindow(),
    },
    {
      state: (caller, emit) => {
        emit({ maximized: popupWindowMgr.requireSender(caller).window.isMaximized() });
        return () => {};
      },
    },
  );
  server.register(browserViewContract, {
    create: (caller, id, src) => overlays.create(caller.sender, id, src),
    load: (caller, id, src) => overlays.load(caller.sender, id, src),
    setRect: (caller, id, rect) => overlays.setRect(caller.sender, id, rect),
    release: (caller, id) => overlays.release(caller.sender, id),
  });
  return () => {
    popupWindowMgr.setStateOutlet(() => {});
    overlays.stop();
  };
}
