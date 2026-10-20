import type { NativeWindowRect } from '../../types';

export interface NativeWindowLayout {
  requestMeasure(): void;
  dispose(): void;
}

// 业务布局只改变位置时，主动调用返回的 requestMeasure。
export function observeNativeWindow(
  host: HTMLElement,
  api: { setRect(rect: NativeWindowRect): void },
): NativeWindowLayout {
  let disposed = false;
  let frame = 0;
  let resolutionQuery: MediaQueryList | undefined;

  function measure() {
    frame = 0;
    if (disposed) return;
    const rect = host.getBoundingClientRect();
    // 零尺寸也上报，由主进程隐藏原生窗口。
    api.setRect({
      x: rect.left,
      y: rect.top,
      width: rect.width,
      height: rect.height,
      dpr: window.devicePixelRatio || 1,
    });
  }

  function requestMeasure() {
    if (!disposed && frame === 0) frame = requestAnimationFrame(measure);
  }

  function watchResolution() {
    resolutionQuery?.removeEventListener('change', onResolutionChange);
    resolutionQuery = window.matchMedia('(resolution: ' + (window.devicePixelRatio || 1) + 'dppx)');
    resolutionQuery.addEventListener('change', onResolutionChange);
  }

  function onResolutionChange() {
    if (disposed) return;
    watchResolution();
    requestMeasure();
  }

  const observer = new ResizeObserver(requestMeasure);
  observer.observe(host);
  window.addEventListener('resize', requestMeasure);
  document.addEventListener('scroll', requestMeasure, true);
  watchResolution();
  requestMeasure();

  return {
    requestMeasure,
    dispose() {
      disposed = true;
      if (frame !== 0) cancelAnimationFrame(frame);
      observer.disconnect();
      window.removeEventListener('resize', requestMeasure);
      document.removeEventListener('scroll', requestMeasure, true);
      resolutionQuery?.removeEventListener('change', onResolutionChange);
    },
  };
}
