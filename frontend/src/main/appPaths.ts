// 路径与进程参数：与 Electron 主进程入口同级（media-service 是它拉起的子进程），
// 命令行参数一律优先，开发和测试脚本靠它覆盖。

import path from 'node:path';

export interface AppPaths {
  // 产物目录：以 UI 进程 exe 所在目录为基准（开发期 electron.exe，产物 folio.exe）
  appDir: string;
  // 配置目录（图标 / bin 资源）：打包时与 exe 同级（scripts/build.py 铺 config/），
  // 开发期用 --config-dir= 指回仓库
  configDir: string;
  mainLogPath: string;
  mediaServiceLogPath: string;
  mediaServiceExe: string;
  pipeName: string;
}

export function argValue(name: string, argv: readonly string[] = process.argv): string {
  const prefix = '--' + name + '=';
  for (const arg of argv) {
    if (arg.startsWith(prefix)) {
      return arg.slice(prefix.length);
    }
  }
  return '';
}

export function resolvePaths(): AppPaths {
  const appDir = path.dirname(process.execPath);
  const mainLogPath = argValue('main-log') || path.join(appDir, 'logs', 'main.log');
  return {
    appDir,
    configDir: argValue('config-dir') || path.join(appDir, 'config'),
    mainLogPath,
    mediaServiceLogPath:
      argValue('media-service-log') || path.join(path.dirname(mainLogPath), 'media-service.log'),
    mediaServiceExe: argValue('media-service-exe') || path.join(appDir, 'media-service.exe'),
    // 每个 UI 进程使用独立的管道名。
    pipeName: argValue('pipe-name') || '\\\\.\\pipe\\folio-' + String(process.pid),
  };
}
