import { Component, Suspense, type ReactNode } from 'react';
import { createRoot } from 'react-dom/client';
import './styles.css';
import { LocaleProvider, useLocale } from './locales';

function ErrorFallback() {
  const { t } = useLocale();
  return <p role="alert">{t('window.failed')}</p>;
}
function LoadingFallback() {
  const { t } = useLocale();
  return <p role="status">{t('window.loading')}</p>;
}

class ErrorBoundary extends Component<{ children: ReactNode }, { failed: boolean }> {
  state = { failed: false };
  static getDerivedStateFromError() {
    return { failed: true };
  }
  componentDidCatch(error: Error) {
    console.error('窗口初始化失败:', error);
  }
  render() {
    return this.state.failed ? <ErrorFallback /> : this.props.children;
  }
}

export function mountApp(page: ReactNode, _initializeWindows = false): () => void {
  const container = document.getElementById('app');
  if (!container) throw new Error('Missing app container');
  const root = createRoot(container);
  root.render(
    <LocaleProvider>
      <ErrorBoundary>
        <Suspense fallback={<LoadingFallback />}>{page}</Suspense>
      </ErrorBoundary>
    </LocaleProvider>,
  );
  const dispose = () => {
    window.removeEventListener('unload', dispose);
    root.unmount();
  };
  window.addEventListener('unload', dispose, { once: true });
  return dispose;
}
