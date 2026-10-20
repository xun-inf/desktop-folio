import type { FolioMainApi, FolioMediaServiceApi, FolioBizModulesApi } from '../preload/preload';
export type {
  MediaStatus,
  NativeWindowRect,
  StreamPreviewFrameMeta,
} from '../main/mediaService/contract';
export type StreamPreviewSubscribe = FolioMediaServiceApi['streamPreviewService']['subscribe'];
declare global {
  interface Window {
    folioMain: FolioMainApi;
    folioMediaService: FolioMediaServiceApi;
    folioBizModules: FolioBizModulesApi;
  }
}
