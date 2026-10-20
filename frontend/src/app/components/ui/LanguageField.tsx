import { useState } from 'react';
import { isSupportedLanguage } from '../../../common/localization-contract';
import { useLocale } from '../../locales';
import { Notice } from './Notice';

// 界面语言选择：写回 folioMain.configuration，经统一事件通道广播到各窗口。
export function LanguageField() {
  const { language, t, setLanguage, pending, error, clearError } = useLocale();
  const [saved, setSaved] = useState(false);
  return (
    <div className="language-field">
      <label htmlFor="language-select">{t('settings.language')}</label>
      <p id="language-description">{t('settings.languageDescription')}</p>
      <select
        id="language-select"
        value={language}
        aria-disabled={pending}
        aria-busy={pending}
        aria-describedby="language-description"
        onChange={(event) => {
          if (pending) {
            event.currentTarget.value = language;
            return;
          }
          const value = event.currentTarget.value;
          if (isSupportedLanguage(value)) {
            setSaved(false);
            void setLanguage(value)
              .then(() => setSaved(true))
              .catch(() => {});
          }
        }}
      >
        <option value="en">English</option>
        <option value="zh-CN">简体中文</option>
      </select>
      {pending && <Notice>{t('settings.saving')}</Notice>}
      {saved && !error && !pending && (
        <Notice onDismiss={() => setSaved(false)}>{t('settings.saved')}</Notice>
      )}
      {error && (
        <Notice tone="error" onDismiss={clearError}>
          {t(error === 'load' ? 'error.configurationLoad' : 'error.configurationSave')}
        </Notice>
      )}
    </div>
  );
}
