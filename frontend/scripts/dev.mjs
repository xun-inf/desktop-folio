import { createServer } from 'vite';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import electron from 'electron';

const root = fileURLToPath(new URL('..', import.meta.url));
const server = await createServer({
  configFile: fileURLToPath(new URL('../vite.config.mjs', import.meta.url)),
});
await server.listen();
server.printUrls();
const env = { ...process.env, VITE_DEV_SERVER_URL: server.resolvedUrls.local[0] };
delete env.ELECTRON_RUN_AS_NODE;
const args = process.argv.slice(2);
const entry = args[0] === '--smoke' ? ['test/window-smoke.mjs', ...args.slice(1)] : ['.', ...args];
const child = spawn(electron, entry, {
  cwd: root,
  env,
  stdio: 'inherit',
  windowsHide: args[0] === '--smoke',
});
let closing = false;
async function close(code = 0) {
  if (closing) return;
  closing = true;
  child.kill();
  await server.close();
  process.exitCode = code;
}
child.once('exit', (code) => {
  void close(code ?? 0);
});
child.once('error', (error) => {
  console.error(error);
  void close(1);
});
process.once('SIGINT', () => {
  void close();
});
process.once('SIGTERM', () => {
  void close();
});
