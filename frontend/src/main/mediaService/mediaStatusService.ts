import type { MediaServiceApi, Status } from './addon.js';
export class MediaStatusService {
  constructor(private readonly native: MediaServiceApi) {}
  getStatus(): Status {
    return this.appStatus(this.native.getStatus());
  }
  onStatus(handler: (status: Status) => void): () => void {
    const subscription = this.native.subscribeStatus((status) => handler(this.appStatus(status)));
    return () => subscription.dispose();
  }
  private appStatus(status: Status): Status {
    const message =
      status.phase === 'ready'
        ? '已连接'
        : status.phase === 'stopped'
          ? '已停止'
          : status.phase === 'disconnected'
            ? '已断开: ' + status.message
            : '连接中…';
    return { ...status, message };
  }
}
