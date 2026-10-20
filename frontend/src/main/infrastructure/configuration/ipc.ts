import type { IpcServer } from '../../../ipc/server.js';
import { configurationContract } from './contract.js';
import type { ConfigurationService } from './service.js';
export function registerConfigurationIpc(server: IpcServer, service: ConfigurationService): void {
  server.register(
    configurationContract,
    {
      get: () => service.get(),
      setLanguage: (_caller, language) => service.setLanguage(language),
    },
    { languageChanged: (_caller, emit) => service.onLanguageChanged(emit) },
  );
}
