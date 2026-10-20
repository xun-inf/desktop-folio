import { useMediaStatus } from '../../hooks/useMediaStatus';
import { useWindowControls } from '../../hooks/useWindowControls';
import { useLocale } from '../../locales';
import { Button } from '../../components/ui/Button';

export function TitleBar() {
  const status = useMediaStatus();
  const { maximized, minimize, toggleMaximize, close } = useWindowControls();
  const { t } = useLocale();
  const label = t(maximized ? 'window.restore' : 'window.maximize');
  const connectionLabel = t(
    status.connected
      ? 'status.connected'
      : status.message === '已停止'
        ? 'status.stopped'
        : status.message === '正在关闭…'
          ? 'status.closing'
          : status.message === '读取连接状态失败'
            ? 'status.failed'
            : status.message.startsWith('已断开')
              ? 'status.disconnected'
              : 'status.connecting',
  );
  return (
    <header className="titlebar">
      <div className="titlebar-info">
        <span className="app-name">Folio</span>
        <div
          className="connection-status"
          id="connection-status"
          role="status"
          title={t('status.mediaService') + ': ' + connectionLabel}
        >
          <span
            className={'connection-dot' + (status.connected ? ' online' : '')}
            id="connection-dot"
            aria-hidden="true"
          />
          <span className="connection-service">{t('status.mediaService')}:</span>
          <span id="connection-label">{connectionLabel}</span>
        </div>
      </div>
      <nav className="window-controls" aria-label={t('window.controls')}>
        <Button
          className="window-button"
          id="window-minimize"
          title={t('window.minimize')}
          aria-label={t('window.minimize')}
          onClick={minimize}
        >
          <svg viewBox="0 0 12 12" aria-hidden="true">
            <path d="M2 6h8" stroke="currentColor" strokeWidth="1.2" />
          </svg>
        </Button>
        <Button
          className="window-button"
          id="window-maximize"
          title={label}
          aria-label={label}
          aria-pressed={maximized}
          onClick={toggleMaximize}
        >
          <svg viewBox="0 0 12 12" aria-hidden="true">
            {maximized ? (
              <>
                <rect
                  x="2.5"
                  y="4.5"
                  width="5"
                  height="5"
                  fill="none"
                  stroke="currentColor"
                  strokeWidth="1.2"
                />
                <path d="M4.5 4.5V2.5h5v5h-2" fill="none" stroke="currentColor" strokeWidth="1.2" />
              </>
            ) : (
              <rect
                x="2.5"
                y="2.5"
                width="7"
                height="7"
                fill="none"
                stroke="currentColor"
                strokeWidth="1.2"
              />
            )}
          </svg>
        </Button>
        <Button
          className="window-button window-button-close"
          id="window-close"
          title={t('window.close')}
          aria-label={t('window.close')}
          onClick={close}
        >
          <svg viewBox="0 0 12 12" aria-hidden="true">
            <path d="M3 3l6 6M9 3l-6 6" stroke="currentColor" strokeWidth="1.2" />
          </svg>
        </Button>
      </nav>
    </header>
  );
}
