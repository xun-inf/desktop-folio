import { useLayoutEffect, useRef, useState, type CSSProperties } from 'react';
export interface BrowserViewProps {
  src: string;
  hidden?: boolean;
  className?: string;
  style?: CSSProperties;
  onReady?: (view: { id: string }) => void;
  emitUrlChange?: (url: string) => void;
  onError?: (error: Error) => void;
}
// DOM measures its placeholder; Main owns and positions the isolated native browser view.
export default function BrowserView(props: BrowserViewProps) {
  const host = useRef<HTMLDivElement>(null),
    latest = useRef(props);
  latest.current = props;
  const [loading, setLoading] = useState(true),
    [error, setError] = useState('');
  useLayoutEffect(() => {
    const id = crypto.randomUUID(),
      api = window.folioMain.browserView;
    let disposed = false,
      created = false,
      inFlight = false;
    const fail = (reason: unknown) => {
      if (disposed) return;
      const value = reason instanceof Error ? reason : new Error(String(reason));
      setLoading(false);
      setError(value.message);
      latest.current.onError?.(value);
    };
    const measure = () => {
      if (disposed || !created || inFlight || !host.current) return;
      const rect = host.current.getBoundingClientRect(),
        style = getComputedStyle(host.current);
      const visible =
        !latest.current.hidden &&
        style.visibility !== 'hidden' &&
        host.current.getClientRects().length > 0;
      inFlight = true;
      void api
        .setRect(id, { x: rect.x, y: rect.y, width: rect.width, height: rect.height, visible })
        .catch(fail)
        .finally(() => {
          inFlight = false;
        });
    };
    setLoading(true);
    setError('');
    const unsubscribe = api.onEvent((event) => {
      if (event.id !== id || disposed) return;
      if (event.type === 'loaded') {
        setLoading(false);
        measure();
      }
      if (event.type === 'navigated' && event.url) latest.current.emitUrlChange?.(event.url);
      if (event.type === 'error') fail(new Error(event.message || '网页加载失败'));
    });
    void api
      .create(id, props.src)
      .then(() => {
        if (disposed) {
          void api.release(id).catch(() => {});
          return;
        }
        created = true;
        setLoading(false);
        latest.current.onReady?.({ id });
        measure();
      })
      .catch(fail);
    const observer = new ResizeObserver(measure);
    observer.observe(host.current!);
    const timer = window.setInterval(measure, 100);
    document.addEventListener('scroll', measure, true);
    window.addEventListener('resize', measure);
    const dispose = () => {
      if (disposed) return;
      disposed = true;
      unsubscribe();
      observer.disconnect();
      clearInterval(timer);
      document.removeEventListener('scroll', measure, true);
      window.removeEventListener('resize', measure);
      window.removeEventListener('pagehide', dispose);
      void api.release(id).catch(() => {});
    };
    window.addEventListener('pagehide', dispose);
    return dispose;
  }, [props.src]);
  return (
    <div
      ref={host}
      className={props.className}
      hidden={props.hidden}
      style={{ height: '100%', ...props.style }}
      aria-busy={loading}
    >
      {error ? (
        <span role="alert">{error}</span>
      ) : loading ? (
        <span role="status">加载中…</span>
      ) : null}
    </div>
  );
}
