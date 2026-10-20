import type { BizModulesApi, TestModuleApi } from './addon.js';
// The native boundary accepts only explicitly declared own data methods, never getters/prototypes.
export function ownMethod(target: object, name: string): (...args: any[]) => any {
  const method = Object.getOwnPropertyDescriptor(target, name)?.value;
  if (typeof method !== 'function') throw new Error('Method unavailable: ' + name);
  return method;
}
export function createModuleRegistry(native: BizModulesApi): { testModule: TestModuleApi } {
  const target = Object.getOwnPropertyDescriptor(native, 'testModule')?.value;
  if (!target || typeof target !== 'object') throw new Error('Module unavailable: testModule');
  const echo = ownMethod(target, 'echo'),
    publish = ownMethod(target, 'publish'),
    subscribe = ownMethod(target, 'subscribe');
  return {
    testModule: {
      echo: (value) => echo.call(target, value),
      publish: (value) => publish.call(target, value),
      subscribe: (event, callback) => {
        const subscription = subscribe.call(target, event, callback);
        if (!subscription || typeof subscription !== 'object')
          throw new Error('Invalid module subscription');
        const dispose = ownMethod(subscription, 'dispose');
        return { dispose: () => dispose.call(subscription) };
      },
    },
  };
}
