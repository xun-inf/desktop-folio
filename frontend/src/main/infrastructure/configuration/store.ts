import fs from 'node:fs';
import path from 'node:path';
import {
  isLanguageSnapshot,
  isSupportedLanguage,
  type LanguageSnapshot,
  type SupportedLanguage,
} from './contract.js';

// 最小配置存储：只持久化语言；登录/账号等其他配置本阶段不恢复。
export class ConfigurationStore {
  private file: string | null = null;
  private language: SupportedLanguage = 'en';
  private revision = 0;
  private readonly listeners = new Set<(snapshot: LanguageSnapshot) => void>();

  initialize(file: string): void {
    if (this.file !== null) throw new Error('Configuration store is already initialized');
    this.file = file;
    try {
      const raw: unknown = JSON.parse(fs.readFileSync(file, 'utf8'));
      if (isLanguageSnapshot(raw)) {
        this.language = raw.language;
        this.revision = raw.revision;
      }
    } catch {
      // 缺失或损坏的配置沿用默认值。
    }
  }

  get(): LanguageSnapshot {
    return { language: this.language, revision: this.revision };
  }

  async setLanguage(value: unknown): Promise<LanguageSnapshot> {
    if (!isSupportedLanguage(value)) throw new Error('Unsupported language');
    const snapshot: LanguageSnapshot = { language: value, revision: this.revision + 1 };
    this.language = value;
    this.revision = snapshot.revision;
    this.persist(snapshot);
    for (const listener of [...this.listeners]) {
      try {
        listener(snapshot);
      } catch {
        /* 广播失败不影响存储。 */
      }
    }
    return snapshot;
  }

  subscribe(listener: (snapshot: LanguageSnapshot) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private persist(snapshot: LanguageSnapshot): void {
    if (this.file === null) return;
    try {
      fs.mkdirSync(path.dirname(this.file), { recursive: true });
      fs.writeFileSync(this.file, JSON.stringify(snapshot));
    } catch {
      // 写入失败不影响会话内生效。
    }
  }
}

export const configurationStore = new ConfigurationStore();
