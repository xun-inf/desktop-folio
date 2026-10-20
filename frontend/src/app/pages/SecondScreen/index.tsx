import { useLayoutEffect, useState } from 'react';
import { Button } from '../../components/ui/Button';
import { Notice } from '../../components/ui/Notice';
import { StreamPreview } from '../../components/StreamPreview';
import { useWindowControls } from '../../hooks/useWindowControls';
import { useLocale } from '../../locales';

const streamId = Number(new URLSearchParams(location.search).get('streamId') ?? 0);

export function SecondScreen() {
  const { t } = useLocale();
  const { close, toggleMaximize } = useWindowControls();
  const [ready, setReady] = useState(false);
  const [error, setError] = useState(false);
  useLayoutEffect(() => {
    window.folioMain.windows.ready();
  }, []);
  return (
    <>
      <header className="business-toolbar" onDoubleClick={toggleMaximize}>
        <strong id="business-title">{t('second.title')}</strong>
        <Button id="business-close" onClick={close}>
          {t('window.close')}
        </Button>
      </header>
      <section className="second-screen">
        {error ? (
          <Notice tone="error" onDismiss={() => setError(false)}>
            {t('error.video')}
          </Notice>
        ) : (
          !ready && <p role="status">{t('second.waiting')}</p>
        )}
        <div className="second-screen-canvas">
          <StreamPreview
            streamId={streamId}
            aria-label={t('second.video')}
            onFrame={() => setReady(true)}
            onError={() => setError(true)}
          />
        </div>
      </section>
    </>
  );
}
