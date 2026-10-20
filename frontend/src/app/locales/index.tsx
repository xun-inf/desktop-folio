import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from 'react';
import {
  isLanguageSnapshot,
  isSupportedLanguage,
  type LanguageSnapshot,
  type SupportedLanguage,
} from '../../common/localization-contract';
import { en, type TranslationKey } from './en';
import { zhCN } from './zh-CN';

interface LocaleContextValue {
  language: SupportedLanguage;
  t(key: TranslationKey): string;
  setLanguage(language: SupportedLanguage): Promise<void>;
  pending: boolean;
  error: 'load' | 'save' | null;
  clearError(): void;
}

const LocaleContext = createContext<LocaleContextValue | null>(null);

export function LocaleProvider({ children }: { children: ReactNode }) {
  const [language, setCurrentLanguage] = useState<SupportedLanguage>('en');
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<LocaleContextValue['error']>(null);
  const revision = useRef(-1);
  const disposed = useRef(false);
  const apply = useCallback((value: LanguageSnapshot) => {
    if (disposed.current || !isLanguageSnapshot(value) || value.revision <= revision.current)
      return;
    revision.current = value.revision;
    setCurrentLanguage(value.language);
    // A successful read or broadcast replaces the English fallback. A separate
    // failed save remains visible until the user retries or dismisses it.
    setError((previous) => (previous === 'load' ? null : previous));
  }, []);
  useEffect(() => {
    disposed.current = false;
    const unsubscribe = window.folioMain.configuration.onLanguageChanged(apply);
    window.folioMain.configuration
      .get()
      .then(apply)
      .catch(() => {
        if (!disposed.current && revision.current < 0) setError('load');
      });
    return () => {
      disposed.current = true;
      unsubscribe();
    };
  }, [apply]);
  useEffect(() => {
    document.documentElement.lang = language;
    const dictionary = language === 'en' ? en : zhCN;
    const pathname = location.pathname;
    if (pathname.endsWith('/settings.html')) document.title = dictionary['settings.title'];
    else if (pathname.endsWith('/secondscreen.html')) document.title = dictionary['second.title'];
  }, [language]);
  const setLanguage = useCallback(
    async (value: SupportedLanguage) => {
      if (!isSupportedLanguage(value) || disposed.current)
        throw new Error('Invalid language request');
      setPending(true);
      setError(null);
      try {
        apply(await window.folioMain.configuration.setLanguage(value));
      } catch {
        if (!disposed.current) setError('save');
        throw new Error('Language could not be saved');
      } finally {
        if (!disposed.current) setPending(false);
      }
    },
    [apply],
  );
  const value = useMemo<LocaleContextValue>(
    () => ({
      language,
      t: (key) => (language === 'en' ? en : zhCN)[key],
      setLanguage,
      pending,
      error,
      clearError: () => setError(null),
    }),
    [language, setLanguage, pending, error],
  );
  return <LocaleContext.Provider value={value}>{children}</LocaleContext.Provider>;
}

export function useLocale(): LocaleContextValue {
  const context = useContext(LocaleContext);
  if (!context) throw new Error('LocaleProvider is required');
  return context;
}
