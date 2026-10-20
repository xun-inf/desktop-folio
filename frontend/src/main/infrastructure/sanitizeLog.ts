// 日志安全投影：错误只保留分类码，URL 去掉凭据与查询串，敏感键值脱敏。

/** 简化的错误分类：保留 message 的可打印片段（不展开对象、不携带堆栈之外的敏感字段）。 */
function classifyDiagnosticError(value: Error): string {
  try {
    const message = String(value.message || value.name || 'error')
      .trim()
      .slice(0, 120);
    return message || 'ERROR';
  } catch {
    return 'ERROR';
  }
}

/** Safe value projection only; redact the complete util.format result separately. */
export function sanitizeLogArgs(args: unknown[]): unknown[] {
  return args.map((value) => {
    try {
      if (value instanceof Error) return { kind: 'error', code: classifyDiagnosticError(value) };
      if (typeof value === 'string') return value.slice(0, 8192);
      if (value === null || typeof value === 'boolean') return value;
      if (typeof value === 'number') return Number.isFinite(value) ? value : '[number omitted]';
      if (Array.isArray(value)) return '[array omitted]';
      if (typeof value === 'object') return '[object omitted]';
      return '[value omitted]';
    } catch {
      return '[value omitted]';
    }
  });
}

/** Call after util.format, so labels and values split across arguments stay safe. */
export function sanitizeLogText(formatted: string): string {
  const singleLine = formatted.slice(0, 32768).replace(/[\x00-\x1f\x7f-\x9f\u2028\u2029]/g, ' ');
  const withoutQueries = singleLine.replace(/[A-Za-z][A-Za-z0-9+.-]*:\/\/[^\s<>"']+/g, (text) => {
    try {
      const url = new URL(text);
      url.username = '';
      url.password = '';
      url.search = '';
      url.hash = '';
      return url.href;
    } catch {
      return '[url omitted]';
    }
  });
  return withoutQueries
    .replace(
      /((?:authorization|set-cookie|cookie|password|passwd|pwd|(?:access_|refresh_)?token|secret|api[-_]?key|credential)["']?\s*[:=]\s*)[^;,]*/gi,
      '$1[redacted]',
    )
    .slice(0, 512);
}
