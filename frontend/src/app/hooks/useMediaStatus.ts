import { useEffect, useState } from 'react';
import type { MediaStatus } from '../types';

// 媒体服务连接状态：初始 getStatus + 统一事件通道订阅。
export function useMediaStatus(): MediaStatus {
  const [status, setStatus] = useState<MediaStatus>({ connected: false, message: '' });
  useEffect(() => {
    let disposed = false;
    const update = (value: MediaStatus) => {
      if (!disposed) setStatus(value);
    };
    const unsubscribe = window.folioMediaService.media.subscribeStatus(update);
    void window.folioMediaService.media
      .getStatus()
      .then(update)
      .catch(() => {});
    return () => {
      disposed = true;
      unsubscribe();
    };
  }, []);
  return status;
}
