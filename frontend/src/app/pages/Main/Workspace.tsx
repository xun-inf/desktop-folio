import { useState, type ReactNode } from 'react';
import BrowserView from '../../components/BrowserView';
import { NativePreview } from '../../components/NativePreview';
import { Button } from '../../components/ui/Button';
import { Notice } from '../../components/ui/Notice';
import { useLocale } from '../../locales';
import { windowsService } from '../../services/windows';

function ToolIcon({ children }: { children: ReactNode }) {
  return (
    <svg
      viewBox="0 0 20 20"
      width="18"
      height="18"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.4"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      {children}
    </svg>
  );
}

// 框架工作台：演示原生预览、浏览器浮层、第二屏与设置等框架能力。
export function Workspace() {
  const { language, t } = useLocale();
  const [error, setError] = useState(false);
  const [showBrowser, setShowBrowser] = useState(false);
  const [placeholder, setPlaceholder] = useState<string | null>(null);
  const previewPage =
    'data:text/html;charset=utf-8,' +
    encodeURIComponent(
      '<!doctype html><html lang="' +
        language +
        '"><meta charset="utf-8"><style>' +
        'body{margin:0;padding:20px;background:#202a36;color:#e6ebf2;font:14px "Microsoft YaHei",sans-serif}' +
        'button{padding:8px 16px;cursor:pointer}p{color:#aebed0}' +
        '</style><h3>' +
        t('preview.overlay') +
        '</h3><p>' +
        t('preview.overlayDescription') +
        '</p>' +
        '<button onclick="this.textContent=' +
        "'" +
        t('preview.overlayClicked') +
        "'" +
        '">' +
        t('preview.overlayInteract') +
        '</button></html>',
    );
  const run = (task: () => Promise<unknown>) => {
    setError(false);
    task().catch(() => {
      setError(true);
    });
  };
  return (
    <>
      {error && (
        <Notice tone="error" onDismiss={() => setError(false)}>
          {t('error.window')}
        </Notice>
      )}
      {placeholder && <Notice onDismiss={() => setPlaceholder(null)}>{placeholder}</Notice>}
      <section id="workspace" className="workspace" aria-labelledby="workspace-title">
        <h2 id="workspace-title" className="visually-hidden">
          {t('main.description')}
        </h2>
        <nav className="workspace-topbar main-toolbar" aria-label={t('main.navigation')}>
          <h1 className="visually-hidden">{t('main.title')}</h1>
          <div className="topbar-actions">
            <Button
              id="open-second-screen"
              onClick={() =>
                run(() =>
                  windowsService.showWindow({
                    page: 'SecondScreen',
                    id: 'second-screen',
                    title: t('second.title'),
                    size: { width: 960, height: 640 },
                  }),
                )
              }
            >
              {t('second.open')}
            </Button>
            <Button
              id="open-settings"
              onClick={() =>
                run(() =>
                  windowsService.showWindow({
                    page: 'Settings',
                    reuse: true,
                    title: t('settings.title'),
                    size: { width: 900, height: 680 },
                  }),
                )
              }
            >
              {t('settings.title')}
            </Button>
            <Button
              id="btn-menu"
              className="icon-button"
              title={t('main.tools.menu')}
              aria-label={t('main.tools.menu')}
              onClick={() => setPlaceholder(t('main.tools.placeholder'))}
            >
              <ToolIcon>
                <path d="M3 5h14M3 10h14M3 15h14" />
              </ToolIcon>
            </Button>
          </div>
        </nav>
        <div className="preview-stack" aria-labelledby="preview-title">
          <h2 id="preview-title" className="visually-hidden">
            {t('preview.title')}
          </h2>
          <NativePreview ariaLabel={t('preview.nativeRegion')} />
          <div className="preview-overlays">
            <Button
              id="toggle-browser-overlay"
              aria-pressed={showBrowser}
              onClick={() => setShowBrowser((value) => !value)}
            >
              {t('preview.overlay')}
            </Button>
          </div>
          {showBrowser && <BrowserView src={previewPage} className="preview-browser-view" />}
        </div>
      </section>
    </>
  );
}
