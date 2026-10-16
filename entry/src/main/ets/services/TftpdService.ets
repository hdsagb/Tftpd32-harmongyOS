import tftpd from 'libtftpd32.so';
import { preferences } from '@kit.ArkData';
import { fileIo as fs } from '@kit.CoreFileKit';

export interface TrfStat {
  id: number;
  bytes: number;
  size: number;
  ret: number;
  dur?: number;
  file?: string;
  opcode?: number;
}

// ArkTS 限制：对象字面量不能作类型声明 → 用单一接口表达事件
export interface TftpdEvent {
  type: string;      // 'log' | 'trf' | 'stat' | 'service' | 'started'
  text?: string;
  trfs?: TrfStat[];
  raw?: string;
}

export class TftpdService {
  private static cfgPath: string = '';
  private static listeners: ((e: TftpdEvent) => void)[] = [];
  static readonly logs: string[] = [];
  static readonly transfers: TrfStat[] = [];

  static async init(ctx: Context): Promise<void> {
    TftpdService.cfgPath = `${ctx.filesDir}/tftpd32.ini`;
    // TFTP 根目录：沙箱内固定目录（对齐原版开箱即用；用户选目录授权放里程碑3）
    try {
      fs.accessSync(`${ctx.filesDir}/tftpboot`);
    } catch (_e) {
      try { fs.mkdirSync(`${ctx.filesDir}/tftpboot`); } catch (_e2) { /* 已存在 */ }
    }
    tftpd.setConfigPath(TftpdService.cfgPath);
    tftpd.setEventCallback((type: string, json: string) => TftpdService.onEvent(type, json));
  }

  private static onEvent(type: string, json: string): void {
    if (type === 'log') {
      TftpdService.logs.push(json);
      if (TftpdService.logs.length > 500) TftpdService.logs.shift();
    } else if (type === 'trf') {
      try {
        const t = JSON.parse(json) as TrfStat;
        const idx = TftpdService.transfers.findIndex(x => x.id === t.id);
        if (idx >= 0) {
          const cur = TftpdService.transfers[idx];
          cur.bytes = t.bytes;
          cur.size = t.size;
          cur.ret = t.ret;
          if (t.dur !== undefined) cur.dur = t.dur;
          if (t.file !== undefined && t.file !== '') cur.file = t.file;
        } else {
          TftpdService.transfers.push(t);
        }
      } catch (_e) { /* 忽略坏帧 */ }
    } else if (type === 'stat') {
      try {
        const obj = JSON.parse(json) as Record<string, Object>;
        const rawList = obj['t'];
        if (Array.isArray(rawList)) {
          const list = rawList as TrfStat[];
          TftpdService.transfers.splice(0, TftpdService.transfers.length);
          for (const t of list) {
            TftpdService.transfers.push(t);
          }
        }
      } catch (_e) { /* 忽略坏帧 */ }
    }
    const ev = TftpdService.parseEvent(type, json);
    TftpdService.listeners.forEach(fn => fn(ev));
  }

  private static parseEvent(type: string, payload: string): TftpdEvent {
    if (type === 'log') return { type: 'log', text: payload };
    if (type === 'trf' || type === 'stat') return { type: type, trfs: TftpdService.transfers };
    return { type: 'service', raw: payload };
  }

  static subscribe(fn: (e: TftpdEvent) => void): void {
    TftpdService.listeners.push(fn);
  }

  static setInt(key: string, val: number): boolean {
    return tftpd.setSettingInt(key, val);
  }

  static setStr(key: string, val: string): boolean {
    return tftpd.setSettingStr(key, val);
  }

  static save(): boolean {
    return tftpd.saveSettings();
  }

  static start(): boolean {
    return tftpd.startService();
  }

  static stop(): void {
    tftpd.stopService();
  }
}
