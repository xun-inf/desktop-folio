import { contract, event, method, singleString, stringValue } from '../../ipc/contract.js';
export const testModuleContract = contract({
  id: 'testModule',
  methods: {
    echo: method(singleString, stringValue),
    publish: method(singleString, stringValue),
  },
  events: { changed: event(stringValue) },
});
