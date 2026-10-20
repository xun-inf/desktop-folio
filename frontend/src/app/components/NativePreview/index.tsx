import { useLayoutEffect, useRef } from 'react';
import { nativeWindowMgr } from '../../shared';
import { observeNativeWindow, type NativeWindowLayout } from './nativeWindow';

// 仅主窗口使用；layoutKey 改变时可主动同步仅位置变化的业务布局。
export function NativePreview({
  name = 'preview',
  visible = true,
  layoutKey,
  ariaLabel = 'Native video region',
}: {
  name?: string;
  visible?: boolean;
  layoutKey?: string | number;
  ariaLabel?: string;
}) {
  const host = useRef<HTMLElement>(null);
  const layout = useRef<NativeWindowLayout | null>(null);
  useLayoutEffect(() => {
    if (!host.current) return;
    let documentRetired = false;
    const retire = () => {
      documentRetired = true;
      layout.current?.dispose();
      layout.current = null;
    };
    window.addEventListener('pagehide', retire, { once: true });
    window.addEventListener('unload', retire, { once: true });
    void nativeWindowMgr.create(name).catch(console.error);
    layout.current = observeNativeWindow(host.current, {
      setRect: (rect) => {
        void nativeWindowMgr.setRect(name, rect).catch(console.error);
      },
    });
    return () => {
      window.removeEventListener('pagehide', retire);
      window.removeEventListener('unload', retire);
      layout.current?.dispose();
      layout.current = null;
      // Main owns resources after document retirement; no IPC may outlive the document.
      if (!documentRetired) void nativeWindowMgr.release(name).catch(console.error);
    };
  }, [name]);
  useLayoutEffect(() => {
    void nativeWindowMgr.setVisible(name, visible).catch(console.error);
    layout.current?.requestMeasure();
  }, [name, visible, layoutKey]);
  return (
    <main
      ref={host}
      className="native-window-host"
      id="native-window-host"
      hidden={!visible}
      aria-label={ariaLabel}
    />
  );
}
