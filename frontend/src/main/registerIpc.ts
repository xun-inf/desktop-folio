import { ipcMain } from 'electron';
import { IpcServer } from '../ipc/server.js';
import { registerConfigurationIpc } from './infrastructure/configuration/ipc.js';
import type { ConfigurationService } from './infrastructure/configuration/service.js';
import { registerBizIpc } from './bizModules/ipc.js';
import type { BizModulesContainer } from './bizModules/bizModules.js';
import { registerMediaIpc } from './mediaService/ipc.js';
import type { MediaServiceContainer } from './mediaService/mediaService.js';
import { registerWindowIpc } from './window/ipc.js';
import { popupWindowMgr } from './window/popupWindow.js';
import type { WindowConfig } from './window/contract.js';
import { logger } from './logger.js';
export function registerIpc(
  config: WindowConfig,
  services: {
    configuration: ConfigurationService;
    media: MediaServiceContainer;
    business: BizModulesContainer;
  },
): { server: IpcServer; dispose(): void } {
  const server = new IpcServer({
    ipcMain,
    resolvePage: (event) => popupWindowMgr.requireSender(event),
    report: (error) => logger.error('IPC cleanup/delivery failed:', error),
  });
  let stopWindows: (() => void) | undefined;
  try {
    registerConfigurationIpc(server, services.configuration);
    registerMediaIpc(server, services.media);
    registerBizIpc(server, services.business);
    stopWindows = registerWindowIpc(server, config, services.media.nativeWindow);
  } catch (error) {
    server.dispose();
    stopWindows?.();
    throw error;
  }
  let disposed = false;
  return {
    server,
    dispose() {
      if (disposed) return;
      disposed = true;
      server.dispose();
      try {
        stopWindows?.();
      } catch (error) {
        logger.error('IPC window cleanup failed:', error);
      }
      logger.info('IPC stopped, subscriptions=' + server.subscriptionCount);
    },
  };
}
