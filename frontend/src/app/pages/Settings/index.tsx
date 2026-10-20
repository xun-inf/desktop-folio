import { useLayoutEffect, useState } from 'react';
import { Button } from '../../components/ui/Button';
import { LanguageField } from '../../components/ui/LanguageField';
import { useWindowControls } from '../../hooks/useWindowControls';
import { useLocale } from '../../locales';

const GROUPS = [
  { id: 'general', title: 'settings.general', description: 'settings.generalDescription' },
  { id: 'stream', title: 'settings.stream', description: 'settings.streamDescription' },
  { id: 'video', title: 'settings.video', description: 'settings.videoDescription' },
  { id: 'audio', title: 'settings.audio', description: 'settings.audioDescription' },
] as const;

export function Settings() {
  const { t } = useLocale();
  const { close } = useWindowControls();
  const [groupId, setGroupId] = useState<(typeof GROUPS)[number]['id']>('general');
  useLayoutEffect(() => {
    window.folioMain.windows.ready();
  }, []);
  const group = GROUPS.find((item) => item.id === groupId) ?? GROUPS[0];
  return (
    <>
      <header className="business-toolbar">
        <strong id="business-title">{t('settings.title')}</strong>
        <Button id="business-close" onClick={close}>
          {t('window.close')}
        </Button>
      </header>
      <div className="settings-page">
        <nav className="settings-navigation" aria-label={t('settings.groups')}>
          {GROUPS.map((item) => (
            <Button
              key={item.id}
              data-group={item.id}
              aria-pressed={groupId === item.id}
              aria-controls="settings-content"
              onClick={() => setGroupId(item.id)}
            >
              {t(item.title)}
            </Button>
          ))}
        </nav>
        <section
          className="settings-content"
          id="settings-content"
          aria-labelledby="settings-group-title"
        >
          <h1 id="settings-group-title">{t(group.title)}</h1>
          <p className="settings-description">{t(group.description)}</p>
          {groupId === 'general' ? (
            <LanguageField />
          ) : (
            <div className="settings-empty">{t('settings.empty')}</div>
          )}
        </section>
      </div>
    </>
  );
}
