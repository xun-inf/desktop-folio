import fs from 'node:fs';
import path from 'node:path';
import { format } from 'node:util';
import { sanitizeLogArgs, sanitizeLogText } from './infrastructure/sanitizeLog.js';

export type LogLevel = 'debug' | 'info' | 'warning' | 'error';
const levels: Record<LogLevel, number> = { debug: 0, info: 1, warning: 2, error: 3 };

class Logger {
  #fileErrorReported = false;
  #logPath: string | null = null;
  level: LogLevel = 'info';

  initialize(logPath: string, level: LogLevel = 'info'): void {
    if (this.#logPath !== null) throw new Error('Logger is already initialized');
    this.#logPath = logPath;
    this.level = level;
  }

  debug(...args: unknown[]): void {
    this.#write('debug', args);
  }
  info(...args: unknown[]): void {
    this.#write('info', args);
  }
  warning(...args: unknown[]): void {
    this.#write('warning', args);
  }
  error(...args: unknown[]): void {
    this.#write('error', args);
  }

  #write(level: LogLevel, args: unknown[]): void {
    if (levels[level] < levels[this.level]) return;
    const line = `${new Date().toISOString()} [main] [${level.toUpperCase()}] ${sanitizeLogText(format(...sanitizeLogArgs(args)))}`;
    if (level === 'error') console.error(line);
    else if (level === 'warning') console.warn(line);
    else if (level === 'debug') console.debug(line);
    else console.info(line);
    if (this.#logPath === null) return;
    try {
      fs.mkdirSync(path.dirname(this.#logPath), { recursive: true });
      fs.appendFileSync(this.#logPath, line + '\n');
      this.#fileErrorReported = false;
    } catch (error) {
      // 日志不可写不影响窗口和 media-service 的生命周期。
      if (!this.#fileErrorReported) {
        this.#fileErrorReported = true;
        console.error('Main log unavailable: WRITE_FAILED');
      }
    }
  }
}

export const logger = new Logger();
