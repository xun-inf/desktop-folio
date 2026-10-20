import {
  contract,
  event,
  isRecord,
  method,
  noArgs,
  type Validator,
} from '../../../ipc/contract.js';
export type SupportedLanguage = 'en' | 'zh-CN';
export interface LanguageSnapshot {
  language: SupportedLanguage;
  revision: number;
}
export const isSupportedLanguage = (value: unknown): value is SupportedLanguage =>
  value === 'en' || value === 'zh-CN';
export const isLanguageSnapshot = (value: unknown): value is LanguageSnapshot =>
  isRecord(value) &&
  Object.keys(value).length === 2 &&
  isSupportedLanguage(value.language) &&
  Number.isSafeInteger(value.revision) &&
  Number(value.revision) >= 0;
const languageArgs: Validator<[SupportedLanguage]> = (value): value is [SupportedLanguage] =>
  Array.isArray(value) && value.length === 1 && isSupportedLanguage(value[0]);
export const configurationContract = contract({
  id: 'configuration',
  methods: {
    get: method(noArgs, isLanguageSnapshot),
    setLanguage: method(languageArgs, isLanguageSnapshot),
  },
  events: { languageChanged: event(isLanguageSnapshot) },
});
