// Entry owns Electron orchestration; bootstrap owns static composition.
import { app, nativeTheme } from 'electron';
import { bootstrap } from './bootstrap.js';
import { logger } from './logger.js';
nativeTheme.themeSource = 'dark';
const application = bootstrap();
void app
  .whenReady()
  .then(() => application.start())
  .catch((error) => {
    logger.error('main start failed:', error);
    void application.shutdown();
  });
