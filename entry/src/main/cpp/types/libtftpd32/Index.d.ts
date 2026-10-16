export const setEventCallback: (cb: (type: string, json: string) => void) => void;
export const setConfigPath: (path: string) => void;
export const startService: () => boolean;
export const stopService: () => void;
export const setSettingInt: (key: string, val: number) => boolean;
export const setSettingStr: (key: string, val: string) => boolean;
export const saveSettings: () => boolean;
export const readSettings: () => boolean;
