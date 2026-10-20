import type { WindowControl, WindowOptions } from '../../common/page-contract';

// 页面窗口门面：业务代码统一经此访问 folioMain.windows。
export const windowsService = {
  showWindow(options: WindowOptions): Promise<string> {
    return window.folioMain.windows.showWindow(options);
  },
  closeWindow(id: string): Promise<void> {
    return window.folioMain.windows.closeWindow(id);
  },
  closeAllWindow(): Promise<void> {
    return window.folioMain.windows.closeAllWindow();
  },
  control(action: WindowControl): Promise<{ maximized: boolean }> {
    return window.folioMain.windows.control(action);
  },
  ready(): void {
    window.folioMain.windows.ready();
  },
  getState(): Promise<{ maximized: boolean }> {
    return window.folioMain.windows.getState();
  },
  onState(callback: (state: { maximized: boolean }) => void): () => void {
    return window.folioMain.windows.onState(callback);
  },
};
