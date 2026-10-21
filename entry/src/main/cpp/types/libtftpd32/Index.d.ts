export const setEventCallback: (cb: (type: string, json: string) => void) => void;
export const setConfigPath: (path: string) => void;
export const startService: () => boolean;
export const stopService: () => void;
export const setSettingInt: (key: string, val: number) => boolean;
export const setSettingStr: (key: string, val: string) => boolean;
export const saveSettings: () => boolean;
export const readSettings: () => boolean;
export const getSettingInt: (key: string) => number;
export const startServiceMask: (mask: number) => boolean;
export const stopServiceMask: (mask: number) => void;
export const startTftpClient: (opcode: number, host: string, port: number,
                               remote: string, local: string, blkSize: number) => boolean;
export const computeMd5: (path: string) => string | undefined;
export const listDirectory: (path: string) => string;
export const getSettingStr: (key: string) => string;
export const getDhcpLeases: () => string;
export const getLocalIPv4s: () => string;
