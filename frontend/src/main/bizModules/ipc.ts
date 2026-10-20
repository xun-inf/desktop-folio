import type { IpcServer } from '../../ipc/server.js';
import type { BizModulesContainer } from './bizModules.js';
import { testModuleContract } from './contract.js';
export function registerBizIpc(server: IpcServer, modules: BizModulesContainer): void {
  server.register(
    testModuleContract,
    {
      echo: (_caller, value) => modules.testModule.echo(value),
      publish: (_caller, value) => modules.testModule.publish(value),
    },
    { changed: (_caller, emit) => modules.testModule.onChanged(emit) },
  );
}
