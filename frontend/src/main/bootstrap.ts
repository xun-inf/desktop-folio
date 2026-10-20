import { app } from 'electron';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { argValue, resolvePaths } from './appPaths.js';
import { mediaService } from './mediaService/mediaService.js';
import { loadMediaService } from './mediaService/addon.js';
import { bizModules, loadBizModules } from './bizModules/index.js';
import { ConfigurationService } from './infrastructure/configuration/service.js';
import { configurationStore } from './infrastructure/configuration/store.js';
import { registerIpc } from './registerIpc.js';
import { logger } from './logger.js';
import { childWindow } from './window/childWindow.js';
import { mainWindow } from './window/mainWindow.js';
import { popupWindowMgr } from './window/popupWindow.js';

// Static application composition. Domain owners remain independent of Electron IPC.
export function bootstrap(): { start(): Promise<void>; shutdown(): Promise<void> } {
  const paths = resolvePaths();
  logger.initialize(paths.mainLogPath);
  const currentDir = path.dirname(fileURLToPath(import.meta.url));
  const devServerUrl = !app.isPackaged ? process.env.VITE_DEV_SERVER_URL : undefined;
  const appBaseUrl =
    devServerUrl || pathToFileURL(path.resolve(currentDir, '..', 'app') + path.sep).href;
  const devtoolsEnabled = !['0', 'false', 'off'].includes(argValue('devtools').toLowerCase());
  const config = {
    pageUrls: {
      Main: new URL('main.html', appBaseUrl).href,
      Settings: new URL('settings.html', appBaseUrl).href,
      SecondScreen: new URL('secondscreen.html', appBaseUrl).href,
    },
    preload: path.resolve(currentDir, '..', 'preload', 'preload.cjs'),
  };
  let ipc: ReturnType<typeof registerIpc> | undefined;
  let closing = false,
    quitAllowed = false;
  let startPromise: Promise<void> | undefined, shutdownPromise: Promise<void> | undefined;
  mainWindow.initialize({
    paths,
    preload: config.preload,
    pageUrl: config.pageUrls.Main,
    devtoolsEnabled,
    canClose: () => quitAllowed,
    onClose: () => {
      void shutdown();
    },
  });
  childWindow.initialize({ pageUrl: config.pageUrls.Settings, preload: config.preload });
  async function start(): Promise<void> {
    if (startPromise) return startPromise;
    startPromise = Promise.resolve().then(async () => {
      if (closing) return;
      logger.info(
        'starting, pid=' + process.pid + ', appDir=' + paths.appDir + ', pipe=' + paths.pipeName,
      );
      mediaService.install(
        loadMediaService(
          path.join(path.dirname(paths.mediaServiceExe), 'node-media-service.node'),
          {
            executable: paths.mediaServiceExe,
            pipeName: paths.pipeName,
            logPath: paths.mediaServiceLogPath,
          },
        ),
      );
      bizModules.install(
        loadBizModules(path.join(path.dirname(paths.mediaServiceExe), 'node-bizmodules.node')),
      );
      configurationStore.initialize(path.join(app.getPath('userData'), 'configuration.json'));
      await bizModules.initialize();
      if (closing) return;
      ipc = registerIpc(config, {
        configuration: new ConfigurationService(configurationStore),
        media: mediaService,
        business: bizModules,
      });
      mainWindow.create();
      childWindow.create();
    });
    return startPromise;
  }
  function shutdown(): Promise<void> {
    if (shutdownPromise) return shutdownPromise;
    closing = true;
    shutdownPromise = Promise.resolve().then(async () => {
      const cleanup = async (name: string, action: () => unknown): Promise<void> => {
        try {
          await action();
        } catch (error) {
          logger.error(name + ' failed:', error);
        }
      };
      await cleanup('window stop', () => popupWindowMgr.stop());
      await cleanup('main window stop', () => mainWindow.stop());
      await cleanup('IPC stop', () => ipc?.dispose());
      ipc = undefined;
      await cleanup('bizModules shutdown', () => bizModules.shutdown());
      try {
        logger.info('bizModules stopped:', bizModules.getStatus());
      } catch {
        /* Startup can fail before install. */
      }
      await cleanup('mediaService stop', () => mediaService.stop());
      quitAllowed = true;
      await cleanup('popup destruction', () => popupWindowMgr.destroy());
      app.quit();
    });
    return shutdownPromise;
  }
  app.on('before-quit', (event) => {
    if (!quitAllowed) {
      event.preventDefault();
      void shutdown();
    }
  });
  app.on('window-all-closed', () => {
    void shutdown();
  });
  return { start, shutdown };
}
