import { useLayoutEffect, useRef, type CanvasHTMLAttributes } from 'react';
import type { StreamPreviewSubscribe } from '../../types';
import { StreamPreviewController } from './streamPreview';

export interface StreamPreviewFrame {
  width: number;
  height: number;
  timestamp: number;
}

export interface StreamPreviewProps
  extends Omit<CanvasHTMLAttributes<HTMLCanvasElement>, 'onError'> {
  streamId: number;
  active?: boolean;
  subscribe?: StreamPreviewSubscribe;
  onFrame?: (frame: StreamPreviewFrame) => void;
  onError?: (error: Error) => void;
}

export function StreamPreview({
  streamId,
  active = true,
  subscribe = window.folioMediaService.streamPreviewService.subscribe,
  onFrame,
  onError,
  ...canvasProps
}: StreamPreviewProps) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const pending = useRef<Promise<void>>(Promise.resolve());
  const callbacks = useRef({ onFrame, onError });
  callbacks.current = { onFrame, onError };

  useLayoutEffect(() => {
    if (!active || !canvas.current) return;
    const target = canvas.current;
    let disposed = false;
    let controller: StreamPreviewController | undefined;
    const report = (error: unknown): void => {
      if (!disposed)
        callbacks.current.onError?.(error instanceof Error ? error : new Error(String(error)));
    };
    // 等待上一轮异步解绑，避免快速切换和 StrictMode 重复订阅同一流。
    const start = pending.current
      .then(async () => {
        if (disposed) return;
        controller = await StreamPreviewController.attach(
          target,
          streamId,
          (id, consume) =>
            subscribe(id, (frame, meta) => {
              if (disposed) {
                frame.close();
                return;
              }
              const info = {
                width: frame.displayWidth,
                height: frame.displayHeight,
                timestamp: frame.timestamp,
              };
              consume(frame, meta);
              try {
                callbacks.current.onFrame?.(info);
              } catch (error) {
                report(error);
              }
            }),
          report,
        );
        if (disposed) await controller.dispose();
      })
      .catch(report);
    return () => {
      disposed = true;
      pending.current = start.then(() => controller?.dispose()).catch(() => {});
    };
  }, [streamId, active, subscribe]);

  return <canvas {...canvasProps} ref={canvas} />;
}
