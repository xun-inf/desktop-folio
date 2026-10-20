import type { ConfigurationStore } from './store.js';
import type { LanguageSnapshot, SupportedLanguage } from './contract.js';
export class ConfigurationService {
  constructor(private readonly store: ConfigurationStore) {}
  get(): LanguageSnapshot {
    return this.store.get();
  }
  setLanguage(language: SupportedLanguage): Promise<LanguageSnapshot> {
    return this.store.setLanguage(language);
  }
  onLanguageChanged(callback: (snapshot: LanguageSnapshot) => void): () => void {
    return this.store.subscribe(callback);
  }
}
